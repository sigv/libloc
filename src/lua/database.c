/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2024 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#include <errno.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include <libloc/database.h>

#include "location.h"
#include "as.h"
#include "compat.h"
#include "country.h"
#include "database.h"
#include "network.h"

typedef struct database {
	struct loc_database* db;
} Database;

static Database* luaL_checkdatabase(lua_State* L, int i) {
	void* userdata = luaL_checkudata(L, i, "location.Database");

	// Throw an error if the argument doesn't match
	luaL_argcheck(L, userdata, i, "Database expected");

	return (Database*)userdata;
}

static int Database_open(lua_State* L) {
	const char* path = NULL;
	FILE* f = NULL;
	int r;

	// Fetch the path
	path = luaL_checkstring(L, 1);

	// Allocate a new object
	Database* self = (Database*)lua_newuserdata(L, sizeof(*self));

	// Set metatable
	luaL_setmetatable(L, "location.Database");

	// Open the database file
	f = fopen(path, "r");
	if (!f)
		return luaL_error(L, "Could not open %s: %s\n", path, strerror(errno));

	// Open the database
	r = loc_database_new(ctx, &self->db, f);

	// Close the file descriptor
	fclose(f);

	// Check for errors
	if (r)
		return luaL_error(L, "Could not open database %s: %s\n", path, strerror(errno));

	return 1;
}

static int Database_gc(lua_State* L) {
	Database* self = luaL_checkdatabase(L, 1);

	// Free database
	if (self->db) {
		loc_database_unref(self->db);
		self->db = NULL;
	}

	return 0;
}

// Description

static int Database_get_description(lua_State* L) {
	Database* self = luaL_checkdatabase(L, 1);

	// Push the description
	lua_pushstring(L, loc_database_get_description(self->db));

	return 1;
}

// License

static int Database_get_license(lua_State* L) {
	Database* self = luaL_checkdatabase(L, 1);

	// Push the license
	lua_pushstring(L, loc_database_get_license(self->db));

	return 1;
}

static int Database_get_vendor(lua_State* L) {
	Database* self = luaL_checkdatabase(L, 1);

	// Push the vendor
	lua_pushstring(L, loc_database_get_vendor(self->db));

	return 1;
}

static int Database_get_as(lua_State* L) {
	struct loc_as* as = NULL;
	int r;

	Database* self = luaL_checkdatabase(L, 1);

	// Fetch number
	uint32_t asn = luaL_checknumber(L, 2);

	// Fetch the AS
	r = loc_database_get_as(self->db, &as, asn);
	if (r) {
		lua_pushnil(L);
		return 1;
	}

	// Create a new AS object
	r = create_as(L, as);
	loc_as_unref(as);

	return r;
}

static int Database_get_country(lua_State* L) {
	struct loc_country* country = NULL;
	int r;

	Database* self = luaL_checkdatabase(L, 1);

	// Fetch code
	const char* code = luaL_checkstring(L, 2);

	// Fetch the country
	r = loc_database_get_country(self->db, &country, code);
	if (r) {
		lua_pushnil(L);
		return 1;
	}

	// Create a new country object
	r = create_country(L, country);
	loc_country_unref(country);

	return r;
}

static int Database_lookup(lua_State* L) {
	struct loc_network* network = NULL;
	int r;

	Database* self = luaL_checkdatabase(L, 1);

	// Require a string
	const char* address = luaL_checkstring(L, 2);

	// Perform lookup
	r = loc_database_lookup_from_string(self->db, address, &network);
	if (r) {
		switch (errno) {
			// Return nil if the network was not found
			case ENOENT:
				lua_pushnil(L);
				return 1;

			default:
				return luaL_error(L, "Could not lookup address %s: %s\n", address, strerror(errno));
		}
	}

	// Create a network object
	r = create_network(L, network);
	loc_network_unref(network);

	return r;
}

static int Database_verify(lua_State* L) {
	FILE* f = NULL;
	int r;

	Database* self = luaL_checkdatabase(L, 1);

	// Fetch path to key
	const char* key = luaL_checkstring(L, 2);

	// Open the keyfile
	f = fopen(key, "r");
	if (!f)
		return luaL_error(L, "Could not open key %s: %s\n", key, strerror(errno));

	// Verify!
	r = loc_database_verify(self->db, f);
	fclose(f);

	// Push result onto the stack
	lua_pushboolean(L, (r == 0));

	return 1;
}

static int Database_next_network(lua_State* L) {
	struct loc_network* network = NULL;
	int r;

	struct loc_database_enumerator* e = lua_touserdata(L, lua_upvalueindex(1));

	// Fetch the next network
	r = loc_database_enumerator_next_network(e, &network);
	if (r)
		return luaL_error(L, "Could not fetch network: %s\n", strerror(errno));

	// If we have received no network, we have reached the end
	if (!network) {
		lua_pushnil(L);
		return 1;
	}

	// Create a network object
	r = create_network(L, network);
	loc_network_unref(network);

	return r;
}

static int Database_list_networks(lua_State* L) {
	struct loc_database_enumerator* e = NULL;
	int r;

	Database* self = luaL_checkdatabase(L, 1);

	// Create a new enumerator
	r = loc_database_enumerator_new(&e, self->db, LOC_DB_ENUMERATE_NETWORKS, 0);
	if (r)
		return luaL_error(L, "Could not create enumerator: %s\n", strerror(errno));

	// Push the enumerator onto the stack
	lua_pushlightuserdata(L, e);

	// Push the closure onto the stack
	lua_pushcclosure(L, Database_next_network, 1);

	return 1;
}

static const struct luaL_Reg database_functions[] = {
	{ "get_as", Database_get_as },
	{ "get_description", Database_get_description },
	{ "get_country", Database_get_country },
	{ "get_license", Database_get_license },
	{ "get_vendor", Database_get_vendor },
	{ "open", Database_open },
	{ "lookup", Database_lookup },
	{ "list_networks", Database_list_networks },
	{ "verify", Database_verify },
	{ "__gc", Database_gc },
	{ NULL, NULL },
};

int register_database(lua_State* L) {
	return register_class(L, "location.Database", database_functions);
}
