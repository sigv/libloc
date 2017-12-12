/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2017 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <loc/libloc.h>
#include <loc/format.h>

#include "libloc-private.h"
#include "as.h"
#include "database.h"
#include "stringpool.h"

struct loc_database {
	struct loc_ctx* ctx;
	int refcount;

	FILE* file;
	unsigned int version;
	time_t created_at;
	off_t vendor;
	off_t description;

	// ASes in the database
	struct loc_database_as_v0* as_v0;
	size_t as_count;

	struct loc_stringpool* pool;
};

static int loc_database_read_magic(struct loc_database* db) {
	struct loc_database_magic magic;

	// Read from file
	size_t bytes_read = fread(&magic, 1, sizeof(magic), db->file);

	// Check if we have been able to read enough data
	if (bytes_read < sizeof(magic)) {
		ERROR(db->ctx, "Could not read enough data to validate magic bytes\n");
		DEBUG(db->ctx, "Read %zu bytes, but needed %zu\n", bytes_read, sizeof(magic));
		return -ENOMSG;
	}

	// Compare magic bytes
	if (memcmp(LOC_DATABASE_MAGIC, magic.magic, strlen(LOC_DATABASE_MAGIC)) == 0) {
		DEBUG(db->ctx, "Magic value matches\n");

		// Parse version
		db->version = ntohs(magic.version);
		DEBUG(db->ctx, "Database version is %u\n", db->version);

		return 0;
	}

	ERROR(db->ctx, "Database format is not compatible\n");

	// Return an error
	return 1;
}

static int loc_database_read_as_section_v0(struct loc_database* db,
		off_t as_offset, size_t as_length) {
	DEBUG(db->ctx, "Reading AS section from %jd (%zu bytes)\n", as_offset, as_length);

	if (as_length > 0) {
		db->as_v0 = mmap(NULL, as_length, PROT_READ,
			MAP_SHARED, fileno(db->file), as_offset);

		if (db->as_v0 == MAP_FAILED)
			return -errno;
	}

	db->as_count = as_length / sizeof(*db->as_v0);

	INFO(db->ctx, "Read %zu ASes from the database\n", db->as_count);

	return 0;
}

static int loc_database_read_header_v0(struct loc_database* db) {
	struct loc_database_header_v0 header;

	// Read from file
	size_t size = fread(&header, 1, sizeof(header), db->file);

	if (size < sizeof(header)) {
		ERROR(db->ctx, "Could not read enough data for header\n");
		return -ENOMSG;
	}

	// Copy over data
	db->created_at  = be64toh(header.created_at);
	db->vendor      = ntohl(header.vendor);
	db->description = ntohl(header.description);

	// Open pool
	off_t pool_offset  = ntohl(header.pool_offset);
	size_t pool_length = ntohl(header.pool_length);

	int r = loc_stringpool_new(db->ctx, &db->pool, 0);
	if (r)
		return r;

	r = loc_stringpool_read(db->pool, db->file, pool_offset, pool_length);
	if (r)
		return r;

	// AS section
	off_t as_offset  = ntohl(header.as_offset);
	size_t as_length = ntohl(header.as_length);

	r = loc_database_read_as_section_v0(db, as_offset, as_length);
	if (r)
		return r;

	return 0;
}

static int loc_database_read_header(struct loc_database* db) {
	switch (db->version) {
		case 0:
			return loc_database_read_header_v0(db);

		default:
			ERROR(db->ctx, "Incompatible database version: %u\n", db->version);
			return 1;
	}
}

static FILE* copy_file_pointer(FILE* f) {
	int fd = fileno(f);

	// Make a copy
	fd = dup(fd);

	return fdopen(fd, "r");
}

LOC_EXPORT int loc_database_new(struct loc_ctx* ctx, struct loc_database** database, FILE* f) {
	struct loc_database* db = calloc(1, sizeof(*db));
	if (!db)
		return -ENOMEM;

	// Reference context
	db->ctx = loc_ref(ctx);
	db->refcount = 1;

	DEBUG(db->ctx, "Database object allocated at %p\n", db);

	// Copy the file pointer and work on that so we don't care if
	// the calling function closes the file
	db->file = copy_file_pointer(f);
	if (!db->file)
		goto FAIL;

	// Read magic bytes
	int r = loc_database_read_magic(db);
	if (r)
		return r;

	// Read the header
	r = loc_database_read_header(db);
	if (r)
		return r;

	*database = db;

	return 0;

FAIL:
	loc_database_unref(db);

	return -errno;
}

LOC_EXPORT struct loc_database* loc_database_ref(struct loc_database* db) {
	db->refcount++;

	return db;
}

static void loc_database_free(struct loc_database* db) {
	DEBUG(db->ctx, "Releasing database %p\n", db);

	// Removing all ASes
	if (db->as_v0) {
		int r = munmap(db->as_v0, db->as_count * sizeof(*db->as_v0));
		if (r)
			ERROR(db->ctx, "Could not unmap AS section: %s\n", strerror(errno));
	}

	loc_stringpool_unref(db->pool);

	// Close file
	if (db->file)
		fclose(db->file);

	loc_unref(db->ctx);
	free(db);
}

LOC_EXPORT struct loc_database* loc_database_unref(struct loc_database* db) {
	if (--db->refcount > 0)
		return NULL;

	loc_database_free(db);
	return NULL;
}

LOC_EXPORT time_t loc_database_created_at(struct loc_database* db) {
	return db->created_at;
}

LOC_EXPORT const char* loc_database_get_vendor(struct loc_database* db) {
	return loc_stringpool_get(db->pool, db->vendor);
}

LOC_EXPORT const char* loc_database_get_description(struct loc_database* db) {
	return loc_stringpool_get(db->pool, db->description);
}

LOC_EXPORT size_t loc_database_count_as(struct loc_database* db) {
	return db->as_count;
}

// Returns the AS at position pos
static int loc_database_fetch_as(struct loc_database* db, struct loc_as** as, off_t pos) {
	if ((size_t)pos >= db->as_count)
		return -EINVAL;

	DEBUG(db->ctx, "Fetching AS at position %jd\n", pos);

	int r;
	switch (db->version) {
		case 0:
			r = loc_as_new_from_database_v0(db->ctx, db->pool, as, db->as_v0 + pos);
			break;

		default:
			return -1;
	}

	if (r == 0) {
		DEBUG(db->ctx, "Got AS%u\n", loc_as_get_number(*as));
	}

	return r;
}

// Performs a binary search to find the AS in the list
LOC_EXPORT int loc_database_get_as(struct loc_database* db, struct loc_as** as, uint32_t number) {
	off_t lo = 0;
	off_t hi = db->as_count - 1;

	while (lo <= hi) {
		off_t i = (lo + hi) / 2;

		// Fetch AS in the middle between lo and hi
		int r = loc_database_fetch_as(db, as, i);
		if (r)
			return r;

		// Check if this is a match
		uint32_t as_number = loc_as_get_number(*as);
		if (as_number == number)
			return 0;

		// If it wasn't, we release the AS and
		// adjust our search pointers
		loc_as_unref(*as);

		if (as_number < number) {
			lo = i + 1;
		} else
			hi = i - 1;
	}

	// Nothing found
	*as = NULL;

	return 0;
}
