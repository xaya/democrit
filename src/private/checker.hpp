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

#ifndef DEMOCRIT_CHECKER_HPP
#define DEMOCRIT_CHECKER_HPP

#include "assetspec.hpp"
#include "private/rpcclient.hpp"
#include "proto/trades.pb.h"
#include "rpc-stubs/xayarpcclient.h"

#include <string>

namespace democrit
{

/**
 * Helper class that implements the verification of trades before the
 * buyer or seller signs them, i.e. the critical things that could result
 * in loss of funds if done wrong.
 */
class TradeChecker
{

private:

  /** Asset specification used for querying the game state.  */
  const AssetSpec& spec;

  /** Xaya RPC connection for checking the blockchain state.  */
  RpcClient<XayaRpcClient>& xaya;

  /** The buyer's account name.  */
  const std::string buyer;

  /** The seller's account name.  */
  const std::string seller;

  /** The asset being traded.  */
  const Asset asset;

  /** The price per unit in satoshi.  */
  const Amount price;

  /** The units of asset being traded.  */
  const Amount units;

public:

  explicit TradeChecker (const AssetSpec& as, RpcClient<XayaRpcClient>& x,
                         const std::string& b, const std::string& s,
                         const Asset& a, const Amount p, const Amount u)
    : spec(as), xaya(x),
      buyer(b), seller(s),
      asset(a), price(p), units(u)
  {}

  TradeChecker () = delete;
  TradeChecker (const TradeChecker&) = delete;
  void operator= (const TradeChecker&) = delete;

  /**
   * Returns the name_update value for the trade, based on the data we have,
   * as a string.  This includes all the stuff like wrapping the move inside
   * the game-ID, and also adding a "dem" move for tracking.
   *
   * Both buyer  and seller themselves call this method, and the seller
   * verifies that the value used in the transaction literally matches the
   * string returned here.  This side-steps potential pitfalls and attack
   * vectors based on weird JSON serialisation.
   */
  std::string GetNameUpdateValue () const;

  /**
   * Computes the total price of the trade in satoshi.  Returns true if all
   * is fine (and the output value is set), and false if e.g. an overflow
   * occurs.
   */
  bool GetTotalSat (Amount& total) const;

  /**
   * Checks if the given trade is valid from the buyer's point of view.  This
   * mostly verifies that the seller actually has the assets and can send them,
   * based on the current GSP and blockchain state.  If it returns true, then
   * the seller's exact name output at which we verified is returned, and is the
   * one that should be used as input into the trading transaction.
   */
  bool CheckForBuyerTrade (proto::OutPoint& nameInput) const;

  /**
   * Compares the "unsigned" and "signed" PSBT (from the buyer's point of view)
   * and verifies that all inputs except one have been signed.  This in
   * particular protects against being tricked into signing everything if the
   * seller impersonates a name in the buyer's wallet.
   */
  bool CheckForBuyerSignature (const std::string& beforeStr,
                               const std::string& afterStr) const;

  /**
   * Verifies that the given PSBT matches the expectations of the seller
   * before signing:  The correct total is paid to their seller-data provided
   * address, and the name is updated with the expected value to their
   * provided name address.
   */
  bool CheckForSellerOutputs (const std::string& psbt,
                              const proto::SellerData& sd) const;

  /**
   * Compares the "unsigned" and "signed" PSBT (from the seller's point of view)
   * and verifies that only the seller's name input has actually been signed.
   */
  bool CheckForSellerSignature (const std::string& beforeStr,
                                const std::string& afterStr,
                                const proto::SellerData& sd) const;

};

} // namespace democrit

#endif // DEMOCRIT_CHECKER_HPP
