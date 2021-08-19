/*
    Democrit - atomic trades for XAYA games
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include <xayautil/jsonutils.hpp>

#include <gtest/gtest.h>

#include <limits>

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
    : checker(spec, env.GetXayaRpc (), "buyer", "seller", "gold", 10, 3)
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

TEST_F (TradeCheckerTests, GetTotalSat)
{
  struct Test
  {
    Amount price;
    Amount units;
    bool expectedSuccess;
    Amount expectedTotal = 0;
  };

  static const Test tests[] =
    {
      {2, 3, true, 6},
      {0, 42, true, 0},
      {
        std::numeric_limits<Amount>::max (),
        std::numeric_limits<Amount>::max (),
        false,
      },
    };

  for (const auto& t : tests)
    {
      const TradeChecker c(spec, env.GetXayaRpc (), "buyer", "seller", "gold",
                           t.price, t.units);
      Amount res;
      ASSERT_EQ (c.GetTotalSat (res), t.expectedSuccess);
      if (t.expectedSuccess)
        {
          ASSERT_EQ (res, t.expectedTotal);
        }
    }
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
  TradeChecker c(spec, env.GetXayaRpc (), "buyer", "seller", "invalid", 1, 1);
  ExpectInvalid (c);
}

TEST_F (TradeCheckerForBuyerTests, BuyerCannotReceive)
{
  TradeChecker c(spec, env.GetXayaRpc (), "uninit", "seller", "gold", 1, 1);
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

class TradeCheckerForBuyerSignatureTests : public TradeCheckerTests
{

protected:

  /**
   * Calls CheckForBuyerSignature with the given before / after "inputs"
   * fields in the decoded PSBT.
   */
  bool
  Check (const std::string& inputsBefore, const std::string& inputsAfter)
  {
    Json::Value before(Json::objectValue);
    before["tx"] = ParseJson ("{}");
    before["inputs"] = ParseJson (inputsBefore);
    env.GetXayaServer ().SetPsbt ("before", before);

    Json::Value after(Json::objectValue);
    after["tx"] = ParseJson ("{}");
    after["inputs"] = ParseJson (inputsAfter);
    env.GetXayaServer ().SetPsbt ("after", after);

    return checker.CheckForBuyerSignature ("before", "after");
  }

};

TEST_F (TradeCheckerForBuyerSignatureTests, AllInputsSigned)
{
  EXPECT_FALSE (Check (R"([
    {}, {}
  ])", R"([
    {"final_scriptwitness": ["foo"]},
    {"final_scriptwitness": ["bar"]}
  ])"));
}

TEST_F (TradeCheckerForBuyerSignatureTests, TooLittleSigned)
{
  EXPECT_FALSE (Check (R"([
    {}, {}, {}
  ])", R"([
    {},
    {"final_scriptwitness": ["foo"]},
    {}
  ])"));
}

TEST_F (TradeCheckerForBuyerSignatureTests, ValidWithoutSellerSignature)
{
  EXPECT_TRUE (Check (R"([
    {}, {}, {}
  ])", R"([
    {},
    {"final_scriptwitness": ["foo"]},
    {"final_scriptwitness": ["bar"]}
  ])"));
}

TEST_F (TradeCheckerForBuyerSignatureTests, ValidWithSellerSignature)
{
  EXPECT_TRUE (Check (R"([
    {},
    {"final_scriptwitness": ["seller"]},
    {}
  ])", R"([
    {"final_scriptwitness": ["foo"]},
    {"final_scriptwitness": ["seller"]},
    {"final_scriptwitness": ["bar"]}
  ])"));
}

/* ************************************************************************** */

class TradeCheckerForSellerOutputsTests : public TradeCheckerTests
{

protected:

  /**
   * Returns the checker's expected name-update value as JSON string literal
   * (so it can be concatenated into the JSON string).
   */
  static std::string
  GetUpdateLiteral (const TradeChecker& c)
  {
    const Json::Value val = c.GetNameUpdateValue ();
    std::ostringstream out;
    out << val;
    return out.str ();
  }

  /**
   * Returns a minimal JSON "vout" value which will check to valid.  The CHI
   * output will be index 0 and the name output index 1.
   */
  static Json::Value
  GetValidVout (const TradeChecker& c)
  {
    Amount total;
    CHECK (c.GetTotalSat (total));

    std::ostringstream chiValue;
    chiValue << xaya::ChiAmountToJson (total);

    return ParseJson (R"([
      {
        "value": )" + chiValue.str () + R"(,
        "scriptPubKey":
          {
            "address": "chi addr"
          }
      },
      {
        "value": 0.01000000,
        "scriptPubKey":
          {
            "address": "name addr",
            "nameOp":
              {
                "op": "name_update",
                "name_encoding": "utf8",
                "value_encoding": "utf8",
                "name": "p/seller",
                "value": )" + GetUpdateLiteral (c) + R"(
              }
          }
      }
    ])");
  }

  /**
   * Calls CheckForSellerOutputs with a PSBT whose ["tx"]["vout"] matches the
   * given decoded JSON.
   */
  bool
  Check (TradeChecker& c, const proto::SellerData& sd, const Json::Value& vout)
  {
    CHECK (vout.isArray ());

    Json::Value tx(Json::objectValue);
    tx["vout"] = vout;
    Json::Value psbt(Json::objectValue);
    psbt["tx"] = tx;

    env.GetXayaServer ().SetPsbt ("psbt", psbt);

    return c.CheckForSellerOutputs ("psbt", sd);
  }

  /**
   * Like Check with explicit seller data, but setting "chi addr" and
   * "name addr" for the addresses we expect.
   */
  bool
  Check (TradeChecker& c, const Json::Value& vout)
  {
    const auto sd = ParseTextProto<proto::SellerData> (R"(
      chi_address: "chi addr"
      name_address: "name addr"
    )");

    return Check (c, sd, vout);
  }

};

TEST_F (TradeCheckerForSellerOutputsTests, Valid)
{
  EXPECT_TRUE (Check (checker, GetValidVout (checker)));
}

TEST_F (TradeCheckerForSellerOutputsTests, LargerPaymentOk)
{
  auto vouts = GetValidVout (checker);
  vouts[0]["value"] = 0.5;
  EXPECT_TRUE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ExtraOutputOk)
{
  auto vouts = GetValidVout (checker);
  vouts.append (ParseJson (R"({
    "value": 10.0,
    "scriptPubKey":
      {
        "address": "change addr"
      }
  })"));
  EXPECT_TRUE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ZeroTotalNeedsNoChiOutput)
{
  TradeChecker c(spec, env.GetXayaRpc (), "buyer", "seller", "gold", 0, 1);

  const auto baseVouts = GetValidVout (c);
  Json::Value vouts(Json::arrayValue);
  vouts.append (baseVouts[1]);

  EXPECT_TRUE (Check (c, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ChiAndNameSameAddress)
{
  auto vouts = GetValidVout (checker);
  vouts[0]["scriptPubKey"]["address"] = "addr";
  vouts[1]["scriptPubKey"]["address"] = "addr";

  const auto sd = ParseTextProto<proto::SellerData> (R"(
    chi_address: "addr"
    name_address: "addr"
  )");

  EXPECT_TRUE (Check (checker, sd, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, TotalOverflow)
{
  const auto max = std::numeric_limits<Amount>::max ();
  TradeChecker c(spec, env.GetXayaRpc (), "buyer", "seller", "gold", max, 2);

  Json::Value vouts = GetValidVout (checker);
  vouts[0]["value"] = 10.0 * max;

  EXPECT_FALSE (Check (c, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, InvalidScriptPubKeyAddresses)
{
  const auto baseVouts = GetValidVout (checker);

  auto vouts = baseVouts;
  vouts[0]["scriptPubKey"] = Json::Value (Json::objectValue);
  EXPECT_FALSE (Check (checker, vouts));

  vouts = baseVouts;
  vouts[0]["scriptPubKey"]["address"] = 42;
  EXPECT_FALSE (Check (checker, vouts));

  vouts = baseVouts;
  vouts[0]["scriptPubKey"] = Json::Value (Json::objectValue);
  vouts[0]["scriptPubKey"]["addresses"] = "chi addr";
  EXPECT_FALSE (Check (checker, vouts));

  vouts = baseVouts;
  vouts[0]["scriptPubKey"] = Json::Value (Json::objectValue);
  vouts[0]["scriptPubKey"]["addresses"] = Json::Value (Json::arrayValue);
  EXPECT_FALSE (Check (checker, vouts));
  vouts[0]["scriptPubKey"]["addresses"].append ("chi addr");
  EXPECT_TRUE (Check (checker, vouts));
  vouts[0]["scriptPubKey"]["addresses"].append ("second addr");
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ChiOutputMissing)
{
  const auto baseVouts = GetValidVout (checker);
  Json::Value vouts(Json::arrayValue);
  vouts.append (baseVouts[1]);
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ChiOutputWrongAddress)
{
  auto vouts = GetValidVout (checker);
  vouts[0]["scriptPubKey"]["address"] = "foo";
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ChiOutputTooLittle)
{
  auto vouts = GetValidVout (checker);
  vouts[0]["value"] = 0.00000029;
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, ChiOutputSplit)
{
  auto vouts = GetValidVout (checker);
  vouts[0]["value"] = 0.00000015;
  vouts.append (vouts[0]);
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameDoesNotCountForChi)
{
  /* In the situation here, the name output goes to the correct CHI address
     and also pays (inside the name) enough to cover the total.  This is still
     not an acceptable situation.  */

  const auto baseVouts = GetValidVout (checker);
  auto vouts = Json::Value (Json::arrayValue);
  vouts.append (baseVouts[1]);
  vouts[0]["scriptPubKey"]["address"] = "addr";
  vouts[0]["value"] = 1.0;

  const auto sd = ParseTextProto<proto::SellerData> (R"(
    chi_address: "addr"
    name_address: "addr"
  )");

  EXPECT_FALSE (Check (checker, sd, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameOutputMissing)
{
  const auto baseVouts = GetValidVout (checker);
  Json::Value vouts(Json::arrayValue);
  vouts.append (baseVouts[0]);
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameOutputWrongAddress)
{
  auto vouts = GetValidVout (checker);
  vouts[1]["scriptPubKey"]["address"] = "foo";
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameOutputWrongValue)
{
  auto vouts = GetValidVout (checker);
  vouts[1]["scriptPubKey"]["nameOp"]["value"] = "foo";
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameOutputWrongName)
{
  /* This is not actually relevant in practice as the seller only signs
     their real input, and thus if the output is for another name, the
     transaction itself is wrong.  But let's check anyway.  */
  auto vouts = GetValidVout (checker);
  vouts[1]["scriptPubKey"]["nameOp"]["name"] = "p/buyer";
  EXPECT_FALSE (Check (checker, vouts));
}

TEST_F (TradeCheckerForSellerOutputsTests, NameOutputWrongOperation)
{
  auto vouts = GetValidVout (checker);
  vouts[1]["scriptPubKey"]["nameOp"]["op"] = "name_register";
  EXPECT_FALSE (Check (checker, vouts));
}

/* ************************************************************************** */

class TradeCheckerForSellerSignatureTests : public TradeCheckerTests
{

protected:

  /**
   * Calls CheckForSellerSignature with the given "tx" field and the
   * given before / after "inputs" fields in the decoded PSBT.  The seller
   * data's name output is always hardcoded to "seller txid" and 12.
   */
  bool
  Check (const std::string& tx, const std::string& inputsBefore,
         const std::string& inputsAfter)
  {
    Json::Value before(Json::objectValue);
    before["tx"] = ParseJson (tx);
    before["inputs"] = ParseJson (inputsBefore);
    env.GetXayaServer ().SetPsbt ("before", before);

    Json::Value after(Json::objectValue);
    after["tx"] = ParseJson (tx);
    after["inputs"] = ParseJson (inputsAfter);
    env.GetXayaServer ().SetPsbt ("after", after);

    const auto sd = ParseTextProto<proto::SellerData> (R"(
      name_output:
        {
          hash: "seller txid"
          n: 12
        }
    )");
    return checker.CheckForSellerSignature ("before", "after", sd);
  }

};

TEST_F (TradeCheckerForSellerSignatureTests, InputNotFound)
{
  EXPECT_FALSE (Check (R"({
    "vin":
      [
        {"txid": "foo", "vout": 1},
        {"txid": "bar", "vout": 10},
        {"txid": "seller txid", "vout": 0},
        {"txid": "baz", "vout": 12}
      ]
  })", R"([
    {}, {}, {}, {}
  ])", R"([
    {}, {}, {}, {}
  ])"));
}

TEST_F (TradeCheckerForSellerSignatureTests, OtherInputSigned)
{
  EXPECT_FALSE (Check (R"({
    "vin":
      [
        {"txid": "foo", "vout": 1},
        {"txid": "seller txid", "vout": 12}
      ]
  })", R"([
    {}, {}
  ])", R"([
    {"final_scriptwitness": ["foo"]},
    {"final_scriptwitness": ["bar"]}
  ])"));
}

TEST_F (TradeCheckerForSellerSignatureTests, OnlyOurInputSigned)
{
  EXPECT_TRUE (Check (R"({
    "vin":
      [
        {"txid": "foo", "vout": 1},
        {"txid": "seller txid", "vout": 12}
      ]
  })", R"([
    {}, {}
  ])", R"([
    {},
    {"final_scriptwitness": ["bar"]}
  ])"));

  EXPECT_TRUE (Check (R"({
    "vin":
      [
        {"txid": "seller txid", "vout": 12},
        {"txid": "foo", "vout": 1}
      ]
  })", R"([
    {},
    {"final_scriptwitness": ["foo"]}
  ])", R"([
    {"final_scriptwitness": ["bar"]},
    {"final_scriptwitness": ["foo"]}
  ])"));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
