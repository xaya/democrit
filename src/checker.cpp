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

} // namespace democrit
