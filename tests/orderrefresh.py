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
Tests refreshing / expiring of orders.
"""

from democrit import ORDER_TIMEOUT
from testcase import NonFungibleTest

import time


class OrderRefreshTest (NonFungibleTest):

  def run (self):
    self.collectPremine ()

    with self.runDemocrit () as d1, \
         self.runDemocrit () as d2:

      self.mainLogger.info ("Setting up test asset...")
      self.sendMove (d2.account, {"m": {"a": "foo", "n": 10}})
      da = self.democritAsset (d2.account, "foo")
      ja = self.jsonAsset (d2.account, "foo")
      self.generate (1)
      self.syncGame ()

      self.mainLogger.info ("Order expiration...")
      with self.runDemocrit () as d3:
        self.assertEqual (d2.addOrder ("ask", da, 10, 1), True)
        self.assertEqual (d3.addOrder ("bid", da, 5, 1), True)
        self.sleepSome ()
        bid = {
          "account": d3.account,
          "id": 0,
          "price_sat": int (5e8),
          "min_units": 1,
          "max_units": 1,
        }
        ask = {
          "account": d2.account,
          "id": 0,
          "price_sat": int (10e8),
          "min_units": 1,
          "max_units": 1,
        }
        self.assertEqual (d1.rpc.getordersforasset (asset=da), {
          "asset": da,
          "bids": [bid],
          "asks": [ask],
        })

      time.sleep (3 * ORDER_TIMEOUT)
      expected = {
        "asset": da,
        "bids": [],
        "asks": [ask],
      }
      own = {
        "account": d2.account,
        "orders": [
          {
            "id": 0,
            "asset": da,
            "type": "ask",
            "price_sat": int (10e8),
            "min_units": 1,
            "max_units": 1,
          },
        ],
      }
      self.assertEqual (d1.rpc.getordersforasset (asset=da), expected)
      self.assertEqual (d2.rpc.getownorders (), own)

      self.mainLogger.info ("Order invalidation...")
      # This sends most of the asset away, but the order is still valid
      # since we are just selling one unit.
      self.sendMove (d2.account, {"t": {"a": ja, "n": 9, "r": "domob"}})
      self.generate (1)
      self.syncGame ()
      time.sleep (3 * ORDER_TIMEOUT)
      self.assertEqual (d1.rpc.getordersforasset (asset=da), expected)
      self.assertEqual (d2.rpc.getownorders (), own)

      # This invalidates the order, which should be noticed at least
      # on the next refresh (by both the other user and the owner).
      self.sendMove (d2.account, {"t": {"a": ja, "n": 1, "r": "domob"}})
      self.generate (1)
      self.syncGame ()
      time.sleep (3 * ORDER_TIMEOUT)
      self.assertEqual (d1.rpc.getordersbyasset (), {})
      self.assertEqual (d2.rpc.getownorders (), {
        "account": d2.account,
        "orders": [],
      })


if __name__ == "__main__":
  OrderRefreshTest ().main ()
