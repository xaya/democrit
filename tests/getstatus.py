#!/usr/bin/env python3

#   Democrit - atomic trades for XAYA games
#   Copyright (C) 2020  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Tests starting of a Democrit daemon and the getstatus RPC.
"""

from testcase import NonFungibleTest


class GetStatusTest (NonFungibleTest):

  def run (self):
    with self.runDemocrit () as d1, \
         self.runDemocrit () as d2:
      for d in [d1, d2]:
        self.assertEqual (d.rpc.getstatus (), {
          "account": d.account,
          "connected": True,
          "gameid": "nf",
        })


if __name__ == "__main__":
  GetStatusTest ().main ()
