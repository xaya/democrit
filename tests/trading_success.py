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
Tests a "normal", successful trade flow between two parties.
"""

from testcase import NonFungibleTest

import json


class TradingSuccessTest (NonFungibleTest):

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

      self.mainLogger.info ("Taking buy order...")
      self.assertEqual (seller.rpc.takeorder (units=2, order={
        "account": buyer.account,
        "id": 0,
        "asset": demAsset,
        "type": "bid",
        "price_sat": int (5e8),
        "max_units": 2,
      }), True)
      self.updateTrades ()
      sellerTrade1 = {
        "state": "pending",
        "counterparty": buyer.account,
        "asset": demAsset,
        "type": "ask",
        "role": "taker",
        "price_sat": int (5e8),
        "units": 2,
      }
      buyerTrade1 = {
        "state": "pending",
        "counterparty": seller.account,
        "asset": demAsset,
        "type": "bid",
        "role": "maker",
        "price_sat": int (5e8),
        "units": 2,
      }
      self.assertEqual (seller.getTrades (), [sellerTrade1])
      self.assertEqual (buyer.getTrades (), [buyerTrade1])
      self.assertEqual (seller.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [],
      })

      # Updating the name and spending more CHI now should work, but it
      # will be based on the unconfirmed output in the wallet and thus
      # not invalidate the trade.  Spending the CHI only works after
      # one confirmation due to wallet handling.
      seller.xaya.name_update (sellerName, "{}")
      self.generate (1)
      buyer.xaya.sendtoaddress (seller.xaya.getnewaddress (), 1)

      # Finish the trade.  It will be seen as successful after 10 confirmations.
      self.generate (8)
      self.updateTrades ()
      self.assertEqual (seller.getTrades (), [sellerTrade1])
      self.assertEqual (buyer.getTrades (), [buyerTrade1])
      self.generate (1)
      self.updateTrades ()
      sellerTrade1["state"] = "success"
      buyerTrade1["state"] = "success"
      self.assertEqual (seller.getTrades (), [sellerTrade1])
      self.assertEqual (buyer.getTrades (), [buyerTrade1])

      # Check the outcome, i.e. that the asset and CHI have been transferred
      # as expected.
      self.expectApproxBalance (seller.xaya, 111)
      self.expectApproxBalance (buyer.xaya, 89)
      self.expectAsset (seller.account, jsonAsset, 8)
      self.expectAsset (buyer.account, jsonAsset, 2)

      self.mainLogger.info ("Taking sell order...")
      self.assertEqual (buyer.rpc.takeorder (units=3, order={
            "account": seller.account,
            "id": 0,
            "asset": demAsset,
            "type": "ask",
            "price_sat": int (10e8),
            "max_units": 5,
      }), True)
      self.updateTrades ()
      sellerTrade2 = {
        "state": "pending",
        "counterparty": buyer.account,
        "asset": demAsset,
        "type": "ask",
        "role": "maker",
        "price_sat": int (10e8),
        "units": 3,
      }
      buyerTrade2 = {
        "state": "pending",
        "counterparty": seller.account,
        "asset": demAsset,
        "type": "bid",
        "role": "taker",
        "price_sat": int (10e8),
        "units": 3,
      }
      self.assertEqual (seller.getTrades (), [sellerTrade2, sellerTrade1])
      self.assertEqual (buyer.getTrades (), [buyerTrade2, buyerTrade1])
      self.assertEqual (buyer.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [],
      })

      self.generate (10)
      self.updateTrades ()
      sellerTrade2["state"] = "success"
      buyerTrade2["state"] = "success"
      self.assertEqual (seller.getTrades (), [sellerTrade1, sellerTrade2])
      self.assertEqual (buyer.getTrades (), [buyerTrade1, buyerTrade2])
      self.assertEqual (buyer.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [
          {
            "account": seller.account,
            "id": 1,
            "price_sat": int (10e8),
            "min_units": 1,
            "max_units": 2,
          },
        ],
      })

      self.expectApproxBalance (seller.xaya, 111 + 30)
      self.expectApproxBalance (buyer.xaya, 89 - 30)
      self.expectAsset (seller.account, jsonAsset, 8 - 3)
      self.expectAsset (buyer.account, jsonAsset, 2 + 3)


if __name__ == "__main__":
  TradingSuccessTest ().main ()
