#!/usr/bin/python3
###############################################################################
#                                                                             #
# libloc - A library to determine the location of someone on the Internet     #
#                                                                             #
# Copyright (C) 2020-2021 IPFire Development Team <info@ipfire.org>           #
#                                                                             #
# This library is free software; you can redistribute it and/or               #
# modify it under the terms of the GNU Lesser General Public                  #
# License as published by the Free Software Foundation; either                #
# version 2.1 of the License, or (at your option) any later version.          #
#                                                                             #
# This library is distributed in the hope that it will be useful,             #
# but WITHOUT ANY WARRANTY; without even the implied warranty of              #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU           #
# Lesser General Public License for more details.                             #
#                                                                             #
###############################################################################

import io
import ipaddress
import logging
import math
import os
import socket
import sys

from .i18n import _
import _location

# Initialise logging
log = logging.getLogger("location.export")
log.propagate = 1

FLAGS = {
	_location.NETWORK_FLAG_ANONYMOUS_PROXY    : "A1",
	_location.NETWORK_FLAG_SATELLITE_PROVIDER : "A2",
	_location.NETWORK_FLAG_ANYCAST            : "A3",
	_location.NETWORK_FLAG_DROP               : "XD",
}

class OutputWriter(object):
	suffix = "networks"
	mode = "w"

	# Enable network flattening (i.e. networks cannot overlap)
	flatten = False

	def __init__(self, f, family=None, prefix=None):
		self.f = f
		self.prefix = prefix
		self.family = family

		# Call any custom initialization
		self.init()

		# Immediately write the header
		self._write_header()

	def init(self):
		"""
			To be overwritten by anything that inherits from this
		"""
		pass

	@classmethod
	def open(cls, filename, *args, **kwargs):
		"""
			Convenience function to open a file
		"""
		if filename:
			f = open(filename, cls.mode)
		elif "b" in cls.mode:
			f = io.BytesIO()
		else:
			f = io.StringIO()

		return cls(f, *args, **kwargs)

	def __repr__(self):
		return "<%s f=%s>" % (self.__class__.__name__, self.f)

	def _write_header(self):
		"""
			The header of the file
		"""
		pass

	def _write_footer(self):
		"""
			The footer of the file
		"""
		pass

	def write(self, network):
		self.f.write("%s\n" % network)

	def finish(self):
		"""
			Called when all data has been written
		"""
		self._write_footer()

		# Flush all output
		self.f.flush()

	def print(self):
		"""
			Prints the entire output line by line
		"""
		if isinstance(self.f, io.BytesIO):
			raise TypeError(_("Won't write binary output to stdout"))

		# Go back to the beginning
		self.f.seek(0)

		# Iterate over everything line by line
		for line in self.f:
			sys.stdout.write(line)


class IpsetOutputWriter(OutputWriter):
	"""
		For ipset
	"""
	suffix = "ipset"

	# The value is being used if we don't know any better
	DEFAULT_HASHSIZE = 64

	# We aim for this many networks in a bucket on average. This allows us to choose
	# how much memory we want to sacrifice to gain better performance. The lower the
	# factor, the faster a lookup will be, but it will use more memory.
	# We will aim for only using three quarters of all buckets to avoid any searches
	# through the linked lists.
	HASHSIZE_FACTOR = 0.75

	def init(self):
		# Count all networks
		self.networks = 0

	@property
	def hashsize(self):
		"""
			Calculates an optimized hashsize
		"""
		# Return the default value if we don't know the size of the set
		if not self.networks:
			return self.DEFAULT_HASHSIZE

		# Find the nearest power of two that is larger than the number of networks
		# divided by the hashsize factor.
		exponent = math.log(self.networks / self.HASHSIZE_FACTOR, 2)

		# Return the size of the hash
		return 2 ** math.ceil(exponent)

	def _write_header(self):
		# This must have a fixed size, because we will write the header again in the end
		self.f.write("create %s hash:net family inet%s" % (
			self.prefix,
			"6" if self.family == socket.AF_INET6 else ""
		))
		self.f.write(" hashsize %8d maxelem 1048576 -exist\n" % self.hashsize)
		self.f.write("flush %s\n" % self.prefix)

	def write(self, network):
		self.f.write("add %s %s\n" % (self.prefix, network))

		# Increment network counter
		self.networks += 1

	def _write_footer(self):
		# Jump back to the beginning of the file
		self.f.seek(0)

		# Rewrite the header with better configuration
		self._write_header()


class NftablesOutputWriter(OutputWriter):
	"""
		For nftables
	"""
	suffix = "set"

	def _write_header(self):
		self.f.write("define %s = {\n" % self.prefix)

	def _write_footer(self):
		self.f.write("}\n")

	def write(self, network):
		self.f.write("	%s,\n" % network)


class XTGeoIPOutputWriter(OutputWriter):
	"""
		Formats the output in that way, that it can be loaded by
		the xt_geoip kernel module from xtables-addons.
	"""
	suffix = "iv"
	mode = "wb"
	flatten = True

	def write(self, network):
		self.f.write(network._first_address)
		self.f.write(network._last_address)


formats = {
	"ipset"    : IpsetOutputWriter,
	"list"     : OutputWriter,
	"nftables" : NftablesOutputWriter,
	"xt_geoip" : XTGeoIPOutputWriter,
}

class Exporter(object):
	def __init__(self, db, writer):
		self.db, self.writer = db, writer

	def export(self, directory, families, countries, asns):
		filename = None

		for family in families:
			log.debug("Exporting family %s" % family)

			writers = {}

			# Create writers for countries
			for country_code in countries:
				if directory:
					filename = self._make_filename(
						directory,
						prefix=country_code,
						suffix=self.writer.suffix,
						family=family,
					)

				writers[country_code] = self.writer.open(filename, family, prefix=country_code)

			# Create writers for ASNs
			for asn in asns:
				if directory:
					filename = self._make_filename(
						directory,
						prefix="AS%s" % asn,
						suffix=self.writer.suffix,
						family=family,
					)

				writers[asn] = self.writer.open(filename, family, prefix="AS%s" % asn)

			# Filter countries from special country codes
			country_codes = [
				country_code for country_code in countries if not country_code in FLAGS.values()
			]

			# Get all networks that match the family
			networks = self.db.search_networks(family=family,
				country_codes=country_codes, asns=asns, flatten=self.writer.flatten)

			# Walk through all networks
			for network in networks:
				# Write matching countries
				try:
					writers[network.country_code].write(network)
				except KeyError:
					pass

				# Write matching ASNs
				try:
					writers[network.asn].write(network)
				except KeyError:
					pass

				# Handle flags
				for flag in FLAGS:
					if network.has_flag(flag):
						# Fetch the "fake" country code
						country = FLAGS[flag]

						try:
							writers[country].write(network)
						except KeyError:
							pass

			# Write everything to the filesystem
			for writer in writers.values():
				writer.finish()

			# Print to stdout
			if not directory:
				for writer in writers.values():
					writer.print()

	def _make_filename(self, directory, prefix, suffix, family):
		filename = "%s.%s%s" % (
			prefix, suffix, "6" if family == socket.AF_INET6 else "4"
		)

		return os.path.join(directory, filename)
