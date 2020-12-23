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

#include <xayautil/jsonutils.hpp>
#include <xayautil/uint256.hpp>

#include <json/json.h>

#include <glog/logging.h>

namespace democrit
{

namespace
{

/**
 * How many blocks back we check when trying to match up the GSP block
 * and the block at which we queried the name UTXO.  Typically they will match
 * directly, but to allow for race conditions with new blocks arriving in
 * between, we check some blocks back.  There is no need to check too many.
 */
constexpr int MAX_BLOCK_ANCESTORS_CHECKED = 3;

/**
 * Returns the full Xaya name (for name operations / lookups) corresponding
 * to a given acount name.
 */
std::string
GetXayaName (const std::string& account)
{
  return "p/" + account;
}

} // anonymous namespace

std::string
TradeChecker::GetNameUpdateValue () const
{
  Json::Value g(Json::objectValue);
  g[spec.GetGameId ()] = spec.GetTransferMove (seller, buyer, asset, units);
  g["dem"] = Json::Value (Json::objectValue);

  Json::Value mv(Json::objectValue);
  mv["g"] = g;

  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;

  return Json::writeString (wbuilder, mv);
}

bool
TradeChecker::GetTotalSat (Amount& total) const
{
  total = price * units;
  CHECK_GT (units, 0);
  if (total / units != price)
    {
      LOG (WARNING)
          << "Total overflow for " << units << " units of price " << price;
      return false;
    }
  return true;
}

namespace
{

/**
 * Checks if the given ancestor block hash is indeed an ancestor of the
 * given child block, according to the Xaya RPC interface.  We check at most
 * n blocks back.
 */
bool
IsBlockAncestor (RpcClient<XayaRpcClient>& rpc,
                 const xaya::uint256& ancestor,
                 const xaya::uint256& child,
                 const int n)
{
  if (ancestor == child)
    return true;

  if (n == 0)
    return false;
  CHECK_GT (n, 0);

  const auto childData = rpc->getblockheader (child.ToHex ());
  CHECK (childData.isObject ());
  const auto prevVal = childData["previousblockhash"];
  if (prevVal.isNull ())
    {
      /* This is the case for the genesis block.  */
      return false;
    }
  CHECK (prevVal.isString ());
  xaya::uint256 parent;
  CHECK (parent.FromHex (prevVal.asString ()))
      << "getblockheader prev block hash is not valid uint256: "
      << childData;

  return IsBlockAncestor (rpc, ancestor, parent, n - 1);
}

} // anonymous namespace

bool
TradeChecker::CheckForBuyerTrade (proto::OutPoint& nameInput) const
{
  if (!spec.IsAsset (asset))
    {
      LOG (WARNING) << "Not a valid asset: " << asset;
      return false;
    }

  if (!spec.CanBuy (buyer, asset, units))
    {
      LOG (WARNING) << buyer << " cannot receive " << units << " of " << asset;
      return false;
    }

  /* We first query for the name output with name_show, and then look up
     that output with gettxout.  The latter gives us the current block hash,
     confirming that the name output was still current at that block hash.

     Then we query the GSP (via AssetSpec), which should confirm that the
     seller can send the assets at the current block.  To verify that the name
     output matches to the GSP state (i.e. was created *before* the GSP state
     we checked), we then make sure the block hash of the name output (from
     gettxout) is the same or one of the last few parent blocks of the
     GSP-returned block hash.  If it is, then all is good.

     This method is correct, because we require (from AssetSpec) that
     the result of CanSell must not change unless an explicit name update
     is done with the seller's name.  Thus if CanSell returns true at a block
     later than when the name output was created, then it will be true for
     any block provided the name is not updated.  So either the trade will
     be valid, or the transaction will be invalid anyway because the name input
     we use is double-spent on the blockchain level.

     This may produce spurious failures in same rare circumstances, like
     when still syncing up or when a reorg happens between the calls.  In those
     cases, we just fail the check and abandon the trade; that should not
     have a lot of impact.  During normal operation, the block hashes
     will most likely actually be identical, or at the most e.g. one new
     block has been attached between the gettxout call and the GSP check.  */

  const auto nameData = xaya->name_show (GetXayaName (seller));
  CHECK (nameData.isObject ());
  const auto txidVal = nameData["txid"];
  CHECK (txidVal.isString ());
  const auto voutVal = nameData["vout"];
  CHECK (voutVal.isUInt ());
  nameInput.Clear ();
  nameInput.set_hash (txidVal.asString ());
  nameInput.set_n (voutVal.asUInt ());

  /* gettxout returns a JSON object when the UTXO is found, or JSON null
     if it does not exist.  libjson-rpc-cpp's generated code does not
     allow having differing return types, though.  So we need to call the
     method directly.  */
  Json::Value params(Json::arrayValue);
  params.append (nameInput.hash ());
  params.append (nameInput.n ());
  const auto utxoData = xaya->CallMethod ("gettxout", params);
  if (utxoData.isNull ())
    {
      LOG (WARNING)
          << "UTXO from name_show is not found; still syncing?\n"
          << nameData;
      return false;
    }
  CHECK (utxoData.isObject ());
  const auto utxoBlockVal = utxoData["bestblock"];
  CHECK (utxoBlockVal.isString ());
  xaya::uint256 utxoBlock;
  CHECK (utxoBlock.FromHex (utxoBlockVal.asString ()))
      << "gettxout 'bestblock' is not valid hash: " << utxoBlockVal;

  xaya::uint256 gspBlock;
  if (!spec.CanSell (seller, asset, units, gspBlock))
    {
      LOG (WARNING) << seller << " cannot send " << units << " of " << asset;
      return false;
    }

  if (!IsBlockAncestor (xaya, utxoBlock, gspBlock, MAX_BLOCK_ANCESTORS_CHECKED))
    {
      LOG (WARNING)
          << "UTXO block is not ancestor of GSP block; still syncing?\n"
          << utxoBlock.ToHex () << " vs\n" << gspBlock.ToHex ();
      return false;
    }

  return true;
}

namespace
{

/**
 * Returns true if the given "scriptPubKey" JSON value (as per Xaya Core's
 * transaction-decoding RPC interface) matches the given address.
 */
bool
MatchesAddress (const Json::Value& scriptPubKey, const std::string& addr)
{
  CHECK (scriptPubKey.isObject ());

  const auto& addresses = scriptPubKey["addresses"];
  if (!addresses.isArray () || addresses.size () != 1)
    return false;

  const auto& addrVal = addresses[0];
  if (!addrVal.isString () || addrVal.asString () != addr)
    return false;

  return true;
}

} // anonymous namespace

bool
TradeChecker::CheckForSellerOutputs (const std::string& psbt,
                                     const proto::SellerData& sd) const
{
  CHECK (sd.has_chi_address () && sd.has_name_address ());

  const auto decoded = xaya->decodepsbt (psbt);
  CHECK (decoded.isObject ());
  const auto& tx = decoded["tx"];
  CHECK (tx.isObject ());
  const auto& vout = tx["vout"];
  CHECK (vout.isArray ());

  Amount expectedTotal;
  if (!GetTotalSat (expectedTotal))
    {
      LOG (WARNING) << "Trade is invalid, could not compute total";
      return false;
    }

  bool foundChi = false;
  bool foundName = false;

  /* Special case:  If the total is zero, there is no need to be paid explicitly
     in a CHI output.  */
  CHECK_GE (expectedTotal, 0);
  if (expectedTotal == 0)
    {
      VLOG (1) << "Total is zero, no need for a CHI output";
      foundChi = true;
    }

  for (const auto& out : vout)
    {
      CHECK (out.isObject ());
      const auto& scriptPubKey = out["scriptPubKey"];
      CHECK (scriptPubKey.isObject ());

      /* Check for name operations first.  If an output is a name operation,
         we do not want to check it (also) for the CHI payment later.  */
      const auto& nameOp = scriptPubKey["nameOp"];
      if (nameOp.isObject ())
        {
          CHECK_EQ (nameOp["name_encoding"].asString (), "utf8")
              << "Xaya Core's -nameencoding should be set to \"utf8\"";
          CHECK_EQ (nameOp["value_encoding"].asString (), "ascii")
              << "Xaya Core's -valueencoding should be set to \"ascii\"";

          if (nameOp["op"].asString () != "name_update")
            continue;
          if (nameOp["name"].asString () != GetXayaName (seller))
            continue;
          if (nameOp["value"].asString () != GetNameUpdateValue ())
            continue;
          if (!MatchesAddress (scriptPubKey, sd.name_address ()))
            continue;

          VLOG (1) << "Found output with expected name update: " << out;
          foundName = true;
          continue;
        }

      /* Not a name operation at all.  */
      CHECK (nameOp.isNull ());

      if (!MatchesAddress (scriptPubKey, sd.chi_address ()))
        continue;
      Amount payment;
      CHECK (xaya::ChiAmountFromJson (out["value"], payment));
      if (payment < expectedTotal)
        continue;
      VLOG (1) << "Found output with expected CHI payment: " << out;
      foundChi = true;
    }

  if (!foundChi)
    {
      LOG (WARNING) << "Expected CHI output not found";
      return false;
    }

  if (!foundName)
    {
      LOG (WARNING) << "Expected name output not found";
      return false;
    }

  return true;
}

} // namespace democrit
