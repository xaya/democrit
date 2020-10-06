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

#ifndef DEMOCRIT_ASSETSPEC_HPP
#define DEMOCRIT_ASSETSPEC_HPP

#include <xayautil/uint256.hpp>

#include <json/json.h>

#include <cstdint>
#include <string>

namespace democrit
{

/** A type of asset, whose meaning is implementation-defined.  */
using Asset = std::string;

/** An amount of asset that can be traded.  */
using Amount = int64_t;

/**
 * This interface defines the game-specific assets that are available
 * for trading, how to verify them in the game state and what moves
 * are used to transfer them in a trade.  This is the main game-specific
 * part that needs to be implemented.
 *
 * The tradable assets themselves are specified by strings, which have
 * an implementation-defined meaning.
 *
 * Note that instances of this class need to be thread-safe.  For instance,
 * when they use libjson-rpc-cpp clients, they need to make sure to synchronise
 * them properly.
 */
class AssetSpec
{

public:

  AssetSpec () = default;
  virtual ~AssetSpec () = default;

  /**
   * Returns the game ID this is for.
   */
  virtual std::string GetGameId () const = 0;

  /**
   * Returns true if the given string is a valid asset.
   */
  virtual bool IsAsset (const Asset& asset) const = 0;

  /**
   * Returns true if the given account name (without p/ prefix) is able
   * to sell the given amount of the asset.  If the function returns true,
   * then hash is set to the block hash at which we did the check.
   *
   * Democrit assumes in general that tradable assets are only affected by
   * explicit moves.  This means that if the player name's current name output
   * was created before the returned block hash, then it is safe to offer to
   * buy those assets if the function returns true.
   */
  virtual bool CanSell (const std::string& name, const Asset& asset,
                        Amount n, xaya::uint256& hash) const = 0;

  /**
   * Returns true if the given account can buy (receive) the given asset.
   * This will usually be the case, but it can ensure for instance that the
   * account has been created already in the game if that is necessary.
   *
   * Even though the result of this check may depend on the current game
   * state and thus be tied to a particular block hash, this is not exposed
   * to the caller.  Instead, we assume that this does not change
   * frequently; or more precisely, ideally if this function returns true
   * for the "current" state, it should remain true in the future forever.
   * Furthermore, the seller of an asset is never at risk, since they
   * will always get CHI (even if the buyer cannot receive the asset
   * in the end).  So it is the buyer's own responsibility to check,
   * and hence nothing security-critical.
   *
   * This function does not need to check the buyer's CHI balance.  That is
   * something that will be enforced automatically during trade negotiation
   * (as the transaction would be invalid otherwise).  Since the set of UTXOs
   * corresponding to one account's wallet is not known, there is nothing
   * else that can be done.
   */
  virtual bool CanBuy (const std::string& name,
                       const Asset& asset, Amount n) const = 0;

  /**
   * Constructs and returns a move (without the game-ID envelope) that
   * transfers the given asset from a sender account to a recipient account.
   * The sender account is who will send the move.
   *
   * This function is only called if CanSell and CanBuy both are true
   * for the sender and recipient, respectively.
   */
  virtual Json::Value GetTransferMove (const std::string& sender,
                                       const std::string& receiver,
                                       const Asset& asset,
                                       Amount n) const = 0;

};

} // namespace democrit

#endif // DEMOCRIT_ASSETSPEC_HPP
