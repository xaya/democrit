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
Tests a trade where the transaction gets conflicted due to an on-chain
double spend of the name or CHI inputs.  We do this with a reorg, i.e. the
trade will actually be confirmed temporarily but then roll back to being
conflicted and confirm as such.
"""

from testcase import NonFungibleTest

import json


class TradingConflictTest (NonFungibleTest):

  def addArguments (self, parser):
    parser.add_argument ("--name_spent", default=False, action="store_true",
                         help="double-spend the seller's name")
    parser.add_argument ("--chi_spent", default=False, action="store_true",
                         help="double-spend the buyer's CHI")

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

      # We build up a long chain where the name or CHI of the buyer
      # are double spent.  We then invalidate that long chain, do the trade,
      # and then reconsider it to make the trade conflict.
      self.mainLogger.info ("Generating reorg chain...")
      self.generate (1)
      reorgBlock = self.rpc.xaya.getbestblockhash ()
      if self.args.name_spent:
        seller.xaya.name_update (sellerName, "{}")
      if self.args.chi_spent:
        buyer.xaya.sendtoaddress (buyer.xaya.getnewaddress (), 1)
      self.generate (20)
      self.rpc.xaya.invalidateblock (reorgBlock)
      self.abandonTransactions (buyer.xaya)
      self.abandonTransactions (seller.xaya)
      self.updateTrades ()

      # Set up and take an order.
      self.mainLogger.info ("Executing trade...")
      self.assertEqual (seller.addOrder ("ask", demAsset, 10, 5), True)
      self.sleepSome ()
      self.assertEqual (buyer.rpc.takeorder (units=2, order={
        "account": seller.account,
        "id": 0,
        "asset": demAsset,
        "type": "ask",
        "price_sat": int (10e8),
        "max_units": 5,
      }), True)
      self.updateTrades ()
      self.generate (1)
      tradeBlock = self.rpc.xaya.getbestblockhash ()
      self.updateTrades ()

      # Check for the expected, pending trade and the outcome
      # on-chain (which is already there after the first confirmation).
      sellerTrade = {
        "state": "pending",
        "counterparty": buyer.account,
        "asset": demAsset,
        "type": "ask",
        "role": "maker",
        "price_sat": int (10e8),
        "units": 2,
      }
      buyerTrade = {
        "state": "pending",
        "counterparty": seller.account,
        "asset": demAsset,
        "type": "bid",
        "role": "taker",
        "price_sat": int (10e8),
        "units": 2,
      }
      self.assertEqual (seller.getTrades (), [sellerTrade])
      self.assertEqual (buyer.getTrades (), [buyerTrade])
      self.assertEqual (buyer.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [],
      })
      self.expectApproxBalance (seller.xaya, 120)
      self.expectApproxBalance (buyer.xaya, 80)
      self.expectAsset (seller.account, jsonAsset, 8)
      self.expectAsset (buyer.account, jsonAsset, 2)
      self.assertEqual (buyer.rpc.getordersforasset (asset=demAsset), {
        "asset": demAsset,
        "bids": [],
        "asks": [],
      })

      # Reorg to the longer chain, which will make the trade fail.
      # We need to get 10 confirmations between first noticing the double spend
      # and when it is actually considered failed.
      self.mainLogger.info ("Reorging to a conflicting chain...")
      self.rpc.xaya.reconsiderblock (reorgBlock)
      self.updateTrades ()
      self.expectApproxBalance (seller.xaya, 100)
      self.expectApproxBalance (buyer.xaya, 100)
      self.expectAsset (seller.account, jsonAsset, 10)
      self.expectAsset (buyer.account, jsonAsset, 0)
      self.generate (10)
      self.updateTrades ()
      sellerTrade["state"] = "failed"
      buyerTrade["state"] = "failed"
      self.assertEqual (seller.getTrades (), [sellerTrade])
      self.assertEqual (buyer.getTrades (), [buyerTrade])
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

  def abandonTransactions (self, rpc):
    """
    Runs listtransactions on the given Xaya Core RPC, and explicitly
    abandons all transactions that are unconfirmed.
    """

    for tx in rpc.listtransactions ():
      if tx["confirmations"] > 0:
        continue

      try:
        rpc.abandontransaction (tx["txid"])
      except:
        pass


if __name__ == "__main__":
  TradingConflictTest ().main ()
