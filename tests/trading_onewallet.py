#!/usr/bin/env python3

#   Democrit - atomic trades for XAYA games
#   Copyright (C) 2021  Autonomous Worlds Ltd
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
Tests trading between two daemons with the same underlying wallet.
This will trip the signature checks built into the verification
logic (i.e. that the counterparty can not trick us into signing
more inputs than expected).

Note that since the buyer signs first in all cases (even though a maker/buyer
does not share the signatures initially), the buyer's check will be
triggered in this test.  For the seller's check, we rely just on the
unit tests present, as that would be harder to cross-check in an
integration test.
"""

from testcase import NonFungibleTest

import json
import time


class TradingOneWalletTest (NonFungibleTest):

  def run (self):
    self.collectPremine ()

    self.rpc.xaya.createwallet ("wallet")

    with self.runDemocrit ("wallet") as buyer, \
         self.runDemocrit ("wallet") as seller:

      self.mainLogger.info ("Setting up test accounts...")
      self.rpc.xaya.sendtoaddress (buyer.xaya.getnewaddress (), 100)
      sellerName = "p/%s" % seller.account
      mv = {"g": {"nf": {"m": {"a": "test", "n": 10}}}}
      opt = {"destAddress": seller.xaya.getnewaddress ()}
      self.rpc.xaya.name_register (sellerName, json.dumps (mv), opt)
      demAsset = self.democritAsset (seller.account, "test")
      jsonAsset = self.jsonAsset (seller.account, "test")
      self.generate (1)
      self.syncGame ()

      self.mainLogger.info ("Setting up orders...")
      self.assertEqual (seller.addOrder ("ask", demAsset, 10, 5), True)
      self.assertEqual (buyer.addOrder ("bid", demAsset, 5, 2), True)
      self.sleepSome ()
      self.assertEqual (buyer.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [
          {
            "account": seller.account,
            "id": 0,
            "price_sat": int (10e8),
            "min_units": 1,
            "max_units": 5,
          },
        ],
      })
      self.assertEqual (seller.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [
          {
            "account": buyer.account,
            "id": 0,
            "price_sat": int (5e8),
            "min_units": 1,
            "max_units": 2,
          },
        ],
        "asks": [],
      })

      self.mainLogger.info ("Taking orders...")
      self.assertEqual (seller.rpc.takeorder (units=2, order={
        "account": buyer.account,
        "id": 0,
        "asset": demAsset,
        "type": "bid",
        "price_sat": int (5e8),
        "max_units": 2,
      }), True)
      self.updateTrades ()
      self.assertEqual (buyer.rpc.takeorder (units=3, order={
            "account": seller.account,
            "id": 0,
            "asset": demAsset,
            "type": "ask",
            "price_sat": int (10e8),
            "max_units": 5,
      }), True)
      self.updateTrades ()

      self.mainLogger.info ("Timing out failed trades...")
      time.sleep (3)
      # We do not care about the details of the trades (there are enough
      # other tests for that), but we should have two abandoned trades
      # in each account.
      for d in [buyer, seller]:
        trades = d.getTrades ()
        self.assertEqual ([t["state"] for t in trades], ["abandoned"] * 2)

      # Since no transactions have been made at all, the balance
      # should actually be exact this time.
      for d in [buyer, seller]:
        self.assertEqual (d.xaya.getbalance (), 100)
      self.expectAsset (seller.account, jsonAsset, 10)
      self.expectAsset (buyer.account, jsonAsset, 0)


if __name__ == "__main__":
  TradingOneWalletTest ().main ()
