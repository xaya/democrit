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
Tests a trade where the seller double-spends the asset (sends to a
third party) before trying to dump on a bid order.  This should fail
the buyer's trade check and lead to the trade timing out in the end.
"""

from testcase import NonFungibleTest

import json
import time


class TradingAssetSpentTest (NonFungibleTest):

  def run (self):
    self.collectPremine ()

    self.rpc.xaya.createwallet ("buyer")
    self.rpc.xaya.createwallet ("seller")

    with self.runDemocrit ("buyer") as buyer, \
         self.runDemocrit ("seller") as seller:

      self.mainLogger.info ("Setting up test accounts...")
      self.rpc.xaya.sendmany ("", {
        seller.xaya.getnewaddress (): 100,
        buyer.xaya.getnewaddress (): 100,
      })
      sellerName = "p/%s" % seller.account
      mv = {"g": {"nf": {"m": {"a": "test", "n": 10}}}}
      opt = {"destAddress": seller.xaya.getnewaddress ()}
      self.rpc.xaya.name_register (sellerName, json.dumps (mv), opt)
      demAsset = self.democritAsset (seller.account, "test")
      jsonAsset = self.jsonAsset (seller.account, "test")
      self.generate (1)
      self.syncGame ()

      self.mainLogger.info ("Setting up a buy order...")
      self.assertEqual (buyer.addOrder ("bid", demAsset, 1, 10), True)
      self.sleepSome ()
      self.assertEqual (seller.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [
          {
            "account": buyer.account,
            "id": 0,
            "price_sat": int (1e8),
            "min_units": 1,
            "max_units": 10,
          },
        ],
        "asks": [],
      })

      # The seller now sends the asset away to a third party (so that
      # the selling move would be invalid) and immediately tries to also
      # take the buy order.  This will work initially, as the orders have not
      # been updated yet, but will fail when the buyer tries to verify
      # the trade against the blockchain.
      #
      # Note:  If timing is bad, this may lead to a flaky test failure!

      self.mainLogger.info ("Trying to double-spend the asset...")
      seller.xaya.name_update (sellerName, json.dumps ({
        "g": {"nf": {"t": {"a": jsonAsset, "n": 5, "r": "third"}}}
      }))
      self.generate (1)
      self.assertEqual (seller.rpc.takeorder (units=6, order={
        "account": buyer.account,
        "id": 0,
        "asset": demAsset,
        "type": "bid",
        "price_sat": int (1e8),
        "max_units": 10,
      }), True)

      self.mainLogger.info ("Timing out the trade...")
      # Note that the start time is tracked in seconds, so even though the
      # timeout is just very short, we need to wait for a few seconds at least
      # for it to work.
      time.sleep (3)
      self.updateTrades ()
      self.assertEqual (seller.getTrades (), [{
        "state": "abandoned",
        "counterparty": buyer.account,
        "asset": demAsset,
        "type": "ask",
        "role": "taker",
        "price_sat": int (1e8),
        "units": 6,
      }])
      self.assertEqual (buyer.getTrades (), [{
        "state": "abandoned",
        "counterparty": seller.account,
        "asset": demAsset,
        "type": "bid",
        "role": "maker",
        "price_sat": int (1e8),
        "units": 6,
      }])
      self.assertEqual (seller.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [
          {
            "account": buyer.account,
            "id": 0,
            "price_sat": int (1e8),
            "min_units": 1,
            "max_units": 10,
          },
        ],
        "asks": [],
      })

      self.generate (1)
      self.expectApproxBalance (seller.xaya, 100)
      self.expectApproxBalance (buyer.xaya, 100)
      self.expectAsset (seller.account, jsonAsset, 5)
      self.expectAsset ("third", jsonAsset, 5)
      self.expectAsset (buyer.account, jsonAsset, 0)


if __name__ == "__main__":
  TradingAssetSpentTest ().main ()
