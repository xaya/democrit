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
Tests basic exchange and validation of orderbooks through Democrit.
"""

from testcase import NonFungibleTest


class OrderbookTest (NonFungibleTest):

  def run (self):
    self.collectPremine ()

    with self.runDemocrit () as d1, \
         self.runDemocrit () as d2:

      self.mainLogger.info ("Setting up test assets...")
      self.sendMove (d1.account, {"m": {"a": "foo", "n": 10}})
      self.sendMove (d2.account, {"m": {"a": "bar", "n": 100}})
      self.generate (1)
      self.syncGame ()

      daFoo = self.democritAsset (d1.account, "foo")
      daBar = self.democritAsset (d2.account, "bar")

      self.mainLogger.info ("Testing invalid orders...")
      self.assertEqual (
          d1.addOrder ("bid", self.democritAsset ("invalid", "invalid"), 1, 1),
          False)
      self.assertEqual (
          d1.addOrder ("ask", self.democritAsset (d2.account, "bar"), 1, 1),
          False)
      self.assertEqual (d1.rpc.getownorders (), {
        "account": d1.account,
        "orders": [],
      })
      self.assertEqual (d2.rpc.getordersbyasset (), {})

      self.mainLogger.info ("Testing valid orders...")
      self.assertEqual (d1.addOrder ("bid", daBar, 1, 2), True)
      self.assertEqual (d1.addOrder ("ask", daFoo, 10, 1), True)
      self.sleepSome ()
      self.assertEqual (d1.rpc.getownorders (), {
        "account": d1.account,
        "orders": [
          {
            "id": 0,
            "asset": daBar,
            "type": "bid",
            "price_sat": int (1e8),
            "min_units": 1,
            "max_units": 2,
          },
          {
            "id": 1,
            "asset": daFoo,
            "type": "ask",
            "price_sat": int (10e8),
            "min_units": 1,
            "max_units": 1,
          },
        ],
      })
      self.assertEqual (d2.rpc.getordersbyasset (), {
        daFoo: {
          "asset": daFoo,
          "bids": [],
          "asks": [
            {
              "account": d1.account,
              "id": 1,
              "price_sat": int (10e8),
              "min_units": 1,
              "max_units": 1,
            },
          ],
        },
        daBar: {
          "asset": daBar,
          "asks": [],
          "bids": [
            {
              "account": d1.account,
              "id": 0,
              "price_sat": int (1e8),
              "min_units": 1,
              "max_units": 2,
            },
          ],
        },
      })

      self.mainLogger.info ("Cancelling an order...")
      d1.rpc.cancelorder (id=42)
      d1.rpc.cancelorder (id=1)
      self.sleepSome ()
      self.assertEqual (d1.rpc.getownorders (), {
        "account": d1.account,
        "orders": [
          {
            "id": 0,
            "asset": daBar,
            "type": "bid",
            "price_sat": int (1e8),
            "min_units": 1,
            "max_units": 2,
          },
        ],
      })
      self.assertEqual (d2.rpc.getordersbyasset (), {
        daBar: {
          "asset": daBar,
          "asks": [],
          "bids": [
            {
              "account": d1.account,
              "id": 0,
              "price_sat": int (1e8),
              "min_units": 1,
              "max_units": 2,
            },
          ],
        },
      })


if __name__ == "__main__":
  OrderbookTest ().main ()
