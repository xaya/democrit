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

#include "mockxaya.hpp"

#include <xayautil/hash.hpp>
#include <xayautil/jsonutils.hpp>

#include <jsonrpccpp/common/exception.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <sstream>

namespace democrit
{

using testing::_;
using testing::Return;

DECLARE_int32 (democrit_feerate_wo_names);

int
GetPortForMockServer ()
{
  static unsigned cnt = 0;
  ++cnt;

  return 2'000 + (cnt % 1'000);
}

/* ************************************************************************** */

MockXayaRpcServer::MockXayaRpcServer (jsonrpc::AbstractServerConnector& conn)
  : XayaRpcServerStub(conn)
{
  bestBlock.SetNull ();

  /* By default, we do not want any of the gmock'ed methods to be called.
     Call expectations should be set explicitly by the tests for methods
     they need.  */
  EXPECT_CALL (*this, CreateFundedPsbt (_, _, _)).Times (0);
  EXPECT_CALL (*this, createpsbt (_, _)).Times (0);
  EXPECT_CALL (*this, NamePsbt (_, _, _, _)).Times (0);
  EXPECT_CALL (*this, joinpsbts (_)).Times (0);
  EXPECT_CALL (*this, walletprocesspsbt (_)).Times (0);
  EXPECT_CALL (*this, sendrawtransaction(_)).Times (0);
}

namespace
{

/**
 * Appends all elements from a JSON array to another JSON array.
 */
void
ExtendJson (Json::Value& out, const Json::Value& in)
{
  CHECK (out.isArray ());
  CHECK (in.isArray ());

  for (const auto& entry : in)
    out.append (entry);
}

} // anonymous namespace

void
MockXayaRpcServer::SetJoinedPsbt (const std::vector<std::string>& psbtsIn,
                                  const std::string& combined)
{
  Json::Value psbtArr(Json::arrayValue);
  auto res = ParseJson (R"({
    "tx":
      {
        "vin": [],
        "vout": []
      },
    "inputs": [],
    "outputs": []
  })");

  for (const auto& part : psbtsIn)
    {
      psbtArr.append (part);
      const auto& decodedPart = psbts.at (part);
      ExtendJson (res["tx"]["vin"], decodedPart["tx"]["vin"]);
      ExtendJson (res["tx"]["vout"], decodedPart["tx"]["vout"]);
      ExtendJson (res["inputs"], decodedPart["inputs"]);
      ExtendJson (res["outputs"], decodedPart["outputs"]);
    }

  SetPsbt (combined, res);
  EXPECT_CALL (*this, joinpsbts (psbtArr)).WillRepeatedly (Return (combined));
}

void
MockXayaRpcServer::PrepareConstructTransaction (
    const std::string& psbt,
    const std::string& seller, const int vout, const proto::SellerData& sd,
    const Amount total, const std::string& move)
{
  FLAGS_democrit_feerate_wo_names = 100;
  const auto jsonTotal = xaya::ChiAmountToJson (total);

  {
    auto outputs = ParseJson ("[{}]");
    outputs[0][sd.chi_address ()] = jsonTotal;

    EXPECT_CALL (*this, CreateFundedPsbt (
        ParseJson ("[]"),
        outputs,
        ParseJson (R"({
          "fee_rate": 100
        })"))
    ).WillRepeatedly (Return ("chi part"));

    auto decoded = ParseJson (R"({
      "tx":
        {
          "vin":
            [
              {"txid": "buyer txid", "vout": 1},
              {"txid": "buyer txid", "vout": 2}
            ],
          "vout":
            [
              {
                "scriptPubKey": {"addresses": ["dummy"]}
              },
              {
                "value": 1.5,
                "scriptPubKey": {"addresses": ["change addr"]}
              }
            ]
        },
      "inputs": [{}, {}],
      "outputs": [{}, {}]
    })");
    auto& chiOut = decoded["tx"]["vout"][0];
    chiOut["value"] = jsonTotal;
    chiOut["scriptPubKey"]["addresses"][0] = sd.chi_address ();
    SetPsbt ("chi part", decoded);
  }

  {
    auto inputs = ParseJson (R"([
      {"txid": "dummy"}
    ])");
    inputs[0]["vout"] = vout;
    inputs[0]["txid"] = seller + " txid";

    auto outputs = ParseJson ("[{}]");
    outputs[0][sd.name_address ()] = 0.01;

    EXPECT_CALL (*this, createpsbt (inputs, outputs))
        .WillRepeatedly (Return ("raw name part"));
    EXPECT_CALL (*this, NamePsbt ("raw name part", 0, "p/" + seller, move))
        .WillRepeatedly (Return ("name part"));

    auto decoded = ParseJson (R"({
      "tx":
        {
          "vin":
            [
              {"txid": "dummy", "vout": 12}
            ],
          "vout":
            [
              {
                "value": 0.01,
                "scriptPubKey":
                  {
                    "nameOp":
                      {
                        "op": "name_update",
                        "name_encoding": "utf8",
                        "value_encoding": "ascii"
                      },
                    "addresses": ["dummy"]
                  }
              }
            ]
        },
      "inputs": [{}],
      "outputs": [{}]
    })");
    decoded["tx"]["vin"][0]["txid"] = seller + " txid";
    auto& nameScript = decoded["tx"]["vout"][0]["scriptPubKey"];
    nameScript["nameOp"]["name"] = "p/" + seller;
    nameScript["nameOp"]["value"] = move;
    nameScript["addresses"][0] = sd.name_address ();
    SetPsbt ("name part", decoded);
  }

  SetJoinedPsbt ({"chi part", "name part"}, psbt);
}

void
MockXayaRpcServer::SetSignedPsbt (const std::string& signedPsbt,
                                  const std::string& psbt,
                                  const std::set<std::string>& signTxids)
{
  auto decoded = psbts.at (psbt);
  CHECK_EQ (decoded["tx"]["vin"].size (), decoded["inputs"].size ())
      << decoded;

  bool complete = true;
  for (unsigned i = 0; i < decoded["tx"]["vin"].size (); ++i)
    {
      auto& inp = decoded["inputs"][i];

      if (signTxids.count (decoded["tx"]["vin"][i]["txid"].asString ()) > 0)
        inp["signed"] = true;

      if (!inp.isMember ("signed") || !inp["signed"].asBool ())
        complete = false;
    }

  Json::Value result(Json::objectValue);
  result["psbt"] = signedPsbt;
  result["complete"] = complete;

  SetPsbt (signedPsbt, decoded);
  EXPECT_CALL (*this, walletprocesspsbt (psbt))
      .WillRepeatedly (Return (result));
}

xaya::uint256
MockXayaRpcServer::GetBlockHash (const unsigned height)
{
  std::ostringstream msg;
  msg << "block " << height;
  return xaya::SHA256::Hash (msg.str ());
}

std::string
MockXayaRpcServer::getnewaddress ()
{
  ++addrCount;

  std::ostringstream res;
  res << "addr " << addrCount;

  return res.str ();
}

Json::Value
MockXayaRpcServer::name_show (const std::string& name)
{
  if (name == "p/invalid" || name.substr (0, 2) != "p/")
    throw jsonrpc::JsonRpcException (-4, "name not found");

  const std::string suffix = name.substr (2);

  Json::Value res(Json::objectValue);
  res["name"] = suffix;
  res["txid"] = suffix + " txid";
  res["vout"] = 12;

  return res;
}

Json::Value
MockXayaRpcServer::gettxout (const std::string& txid, const int vout)
{
  if (utxos.count (std::make_pair (txid, vout)) == 0)
    return Json::Value ();

  Json::Value res(Json::objectValue);
  res["bestblock"] = bestBlock.ToHex ();

  return res;
}

Json::Value
MockXayaRpcServer::getblockheader (const std::string& hashStr)
{
  xaya::uint256 hash;
  if (!hash.FromHex (hashStr))
    throw jsonrpc::JsonRpcException (-8, "block hash is not uint256");

  for (unsigned h = 0; h < 1'000; ++h)
    if (hash == GetBlockHash (h))
      {
        Json::Value res(Json::objectValue);
        res["hash"] = hash.ToHex ();
        res["height"] = static_cast<Json::Int> (h);
        res["nextblockhash"] = GetBlockHash (h + 1).ToHex ();

        if (h > 0)
          res["previousblockhash"] = GetBlockHash (h - 1).ToHex ();

        return res;
      }

  throw jsonrpc::JsonRpcException (-5, "unknown block hash");
}

Json::Value
MockXayaRpcServer::decodepsbt (const std::string& psbt)
{
  const auto mit = psbts.find (psbt);
  if (mit == psbts.end ())
    throw jsonrpc::JsonRpcException (-22, "unknown psbt: " + psbt);
  return mit->second;
}

Json::Value
MockXayaRpcServer::walletcreatefundedpsbt (const Json::Value& inputs,
                                           const Json::Value& outputs,
                                           const int lockTime,
                                           const Json::Value& options)
{
  CHECK_EQ (lockTime, 0) << "lockTime should be passed as zero";

  Json::Value res(Json::objectValue);
  res["psbt"] = CreateFundedPsbt (inputs, outputs, options);

  return res;
}

Json::Value
MockXayaRpcServer::namepsbt (const std::string& psbt, const int vout,
                             const Json::Value& nameOp)
{
  CHECK (nameOp.isObject ());
  CHECK_EQ (nameOp.size (), 3);

  const auto& opVal = nameOp["op"];
  CHECK (opVal.isString ());
  CHECK_EQ (opVal.asString (), "name_update");

  const auto& nameVal = nameOp["name"];
  CHECK (nameVal.isString ());

  const auto& valueVal = nameOp["value"];
  CHECK (valueVal.isString ());

  Json::Value res(Json::objectValue);
  res["psbt"] = NamePsbt (psbt, vout,
                          nameVal.asString (), valueVal.asString ());

  return res;
}

std::string
MockXayaRpcServer::combinepsbt (const Json::Value& inputPsbts)
{
  CHECK (inputPsbts.isArray ());
  CHECK_GT (inputPsbts.size (), 0);

  Json::Value decoded = psbts.at (inputPsbts[0].asString ());
  std::ostringstream outputName;
  outputName << inputPsbts[0].asString ();

  for (unsigned i = 1; i < inputPsbts.size (); ++i)
    {
      const auto& curName = inputPsbts[i].asString ();
      outputName << " + " << curName;
      const auto& cur = psbts.at (curName);

      CHECK_EQ (decoded["tx"], cur["tx"]);
      CHECK_EQ (decoded["outputs"], cur["outputs"]);

      auto& outputInputs = decoded["inputs"];
      CHECK (outputInputs.isArray ());
      const auto& curInputs = cur["inputs"];
      CHECK (curInputs.isArray ());
      CHECK_EQ (outputInputs.size (), curInputs.size ());

      for (unsigned j = 0; j < outputInputs.size (); ++j)
        {
          const auto& inp = curInputs[j];
          if (inp.isMember ("signed") && inp["signed"].asBool ())
            outputInputs[j] = inp;
        }
    }

  SetPsbt (outputName.str (), decoded);
  return outputName.str ();
}

Json::Value
MockXayaRpcServer::finalizepsbt (const std::string& psbt)
{
  const auto& decoded = psbts.at (psbt);
  const auto& inputs = decoded["inputs"];
  CHECK (inputs.isArray ());

  bool complete = true;
  for (const auto& inp : inputs)
    {
      if (!inp.isMember ("signed") || !inp["signed"].asBool ())
        {
          complete = false;
          break;
        }
    }

  Json::Value res(Json::objectValue);
  res["complete"] = complete;
  if (complete)
    res["hex"] = "rawtx " + psbt;
  else
    res["psbt"] = psbt;

  return res;
}

/* ************************************************************************** */

void
MockDemGsp::SetPending (const std::string& btxid)
{
  btxids[btxid] = ParseJson (R"({
    "state": "pending"
  })");
}

void
MockDemGsp::SetConfirmed (const std::string& btxid, const unsigned h)
{
  auto data = ParseJson (R"({
    "state": "confirmed"
  })");
  data["height"] = static_cast<Json::Int> (h);
  btxids[btxid] = data;
}

Json::Value
MockDemGsp::checktrade (const std::string& btxid)
{
  Json::Value res(Json::objectValue);
  res["height"] = static_cast<Json::Int> (currentHeight);

  const auto mit = btxids.find (btxid);
  if (mit != btxids.end ())
    res["data"] = mit->second;
  else
    res["data"] = ParseJson (R"({
      "state": "unknown"
    })");

  return res;
}

/* ************************************************************************** */

} // namespace democrit
