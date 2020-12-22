/*
    Democrit - atomic trades for XAYA games
    Copyright (C) 2020  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "private/checker.hpp"

#include "mockxaya.hpp"
#include "testutils.hpp"

#include <gtest/gtest.h>

namespace democrit
{
namespace
{

DEFINE_PROTO_MATCHER (EqualsOutPoint, OutPoint)

/* ************************************************************************** */

class TradeCheckerTests : public testing::Test
{

protected:

  TestEnvironment<MockXayaRpcServer> env;
  TestAssets spec;

  TradeChecker checker;

  TradeCheckerTests ()
    : checker(spec, env.GetXayaRpc (), "buyer", "seller", "gold", 3)
  {
    /* By default, the setting is such that the game allows the trade.  Tests
       for this will overwrite the data as needed.  */
    spec.InitialiseAccount ("buyer");
    spec.SetBalance ("seller", "gold", 10);
  }

  /**
   * Sets the best block hash in both the asset spec (GSP) and the mock xaya
   * RPC server to the block hash at the given height.
   */
  void
  SetBestBlock (const unsigned h)
  {
    const auto hash = env.GetXayaServer ().GetBlockHash (h);
    env.GetXayaServer ().SetBestBlock (hash);
    spec.SetBlock (hash);
  }

};

TEST_F (TradeCheckerTests, NameUpdateValue)
{
  EXPECT_EQ (
      checker.GetNameUpdateValue (),
      R"({"g":{"dem":{},"test":{"amount":3,"asset":"gold","to":"buyer"}}})");
}

/* ************************************************************************** */

class TradeCheckerForBuyerTests : public TradeCheckerTests
{

protected:

  /**
   * Expects that the checker returns that its trade is invalid for
   * the buyer.
   */
  static void
  ExpectInvalid (const TradeChecker& checker)
  {
    proto::OutPoint nameInput;
    EXPECT_FALSE (checker.CheckForBuyerTrade (nameInput));
  }

  /**
   * Expects that the checker returns that its trade is valid for the buyer.
   * The method returns the name outpoint checked.
   */
  static proto::OutPoint
  ExpectValid (const TradeChecker& checker)
  {
    proto::OutPoint nameInput;
    EXPECT_TRUE (checker.CheckForBuyerTrade (nameInput));
    return nameInput;
  }

};

TEST_F (TradeCheckerForBuyerTests, InvalidAsset)
{
  TradeChecker c(spec, env.GetXayaRpc (), "buyer", "seller", "invalid", 1);
  ExpectInvalid (c);
}

TEST_F (TradeCheckerForBuyerTests, BuyerCannotReceive)
{
  TradeChecker c(spec, env.GetXayaRpc (), "uninit", "seller", "gold", 1);
  ExpectInvalid (c);
}

TEST_F (TradeCheckerForBuyerTests, NameUtxoDoesNotExist)
{
  /* This test simulates a situation where name_show returns an outpoint
     that is not a valid UTXO, e.g. because the name has just been updated.  */
  ExpectInvalid (checker);
}

TEST_F (TradeCheckerForBuyerTests, SellerCannotSend)
{
  SetBestBlock (10);
  env.GetXayaServer ().AddUtxo ("seller txid", 12);
  spec.SetBalance ("seller", "gold", 2);
  ExpectInvalid (checker);
}

TEST_F (TradeCheckerForBuyerTests, NameNotAnAncestor)
{
  /* This situation may happen in practice if there is a reorg or something
     around the time when the name output from the UTXO set and the GSP
     are queried.  This is unlikely to happen in practice, but makes it
     impossible (with the current implementation at least) to prove
     that the trade is safe.

     In the more typical case that the seller tries to cheat by "double
     spending" the assets while the trade is going on, we will most likely
     fail instead with "cannot send" or actually construct the transaction
     but it won't confirm since the name input itself is double-spent on the
     blockchain level.

     The ancestor check is still important, to catch e.g. the following
     situation:  Let's say, the seller spends the assets being traded with
     a name update in block N.  But for some reason, the GSP is at block N-1
     when we query it (e.g. because it lags behind or there is a reorg).
     Then without the ancestor check, we might see that the asset can be
     sold (at block N-1) and construct a transaction using the new name output
     from block N, which would lead to an invalid trade.  */

  spec.SetBlock (env.GetXayaServer ().GetBlockHash (10));
  env.GetXayaServer ().SetBestBlock (env.GetXayaServer ().GetBlockHash (11));

  env.GetXayaServer ().AddUtxo ("seller txid", 12);
  ExpectInvalid (checker);
}

TEST_F (TradeCheckerForBuyerTests, AncestorCheckHitsGenesis)
{
  spec.SetBlock (env.GetXayaServer ().GetBlockHash (2));
  env.GetXayaServer ().SetBestBlock (env.GetXayaServer ().GetBlockHash (10));

  env.GetXayaServer ().AddUtxo ("seller txid", 12);
  ExpectInvalid (checker);
}

TEST_F (TradeCheckerForBuyerTests, ValidSameBlock)
{
  SetBestBlock (10);
  env.GetXayaServer ().AddUtxo ("seller txid", 12);

  const auto outpoint = ExpectValid (checker);
  EXPECT_THAT (outpoint, EqualsOutPoint (R"(
    hash: "seller txid"
    n: 12
  )"));
}

TEST_F (TradeCheckerForBuyerTests, ValidAncestorBlock)
{
  spec.SetBlock (env.GetXayaServer ().GetBlockHash (10));
  env.GetXayaServer ().SetBestBlock (env.GetXayaServer ().GetBlockHash (7));

  env.GetXayaServer ().AddUtxo ("seller txid", 12);
  ExpectValid (checker);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
