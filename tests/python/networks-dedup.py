#!/usr/bin/python3
###############################################################################
#                                                                             #
# libloc - A library to determine the location of someone on the Internet     #
#                                                                             #
# Copyright (C) 2024 IPFire Development Team <info@ipfire.org>                #
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

import location
import os
import tempfile
import unittest

class Test(unittest.TestCase):
	def __test(self, inputs, outputs):
		"""
			Takes a list of networks that are written to the database and
			compares the result with the second argument.
		"""
		with tempfile.NamedTemporaryFile() as f:
			w = location.Writer()

			# Add all inputs
			for network, cc, asn in inputs:
				n = w.add_network(network)

				# Add CC
				if cc:
					n.country_code = cc

				# Add ASN
				if asn:
					n.asn = asn

			# Write file
			w.write(f.name)

			# Re-open the database
			db = location.Database(f.name)

			# Check if the output matches what we expect
			self.assertCountEqual(
				outputs, ["%s" % network for network in db.networks],
			)

	def test_dudup_simple(self):
		"""
			Creates a couple of redundant networks and expects fewer being written
		"""
		self.__test(
			(
				("10.0.0.0/8", None, None),
				("10.0.0.0/16", None, None),
				("10.0.0.0/24", None, None),
			),

			# Everything should be put into the /8 subnet
			("10.0.0.0/8",),
		)

	def test_dedup_noop(self):
		"""
			Nothing should be changed here
		"""
		self.maxDiff = None

		networks = (
			("10.0.0.0/8", None, None),
			("20.0.0.0/8", None, None),
			("30.0.0.0/8", None, None),
			("40.0.0.0/8", None, None),
			("50.0.0.0/8", None, None),
			("60.0.0.0/8", None, None),
			("70.0.0.0/8", None, None),
			("80.0.0.0/8", None, None),
			("90.0.0.0/8", None, None),
		)

		# The input should match the output
		self.__test(networks, [network for network, cc, asn in networks])


if __name__ == "__main__":
	unittest.main()
