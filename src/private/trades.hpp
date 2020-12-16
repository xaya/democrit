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

#ifndef DEMOCRIT_TRADES_HPP
#define DEMOCRIT_TRADES_HPP

#include "assetspec.hpp"
#include "private/myorders.hpp"
#include "private/rpcclient.hpp"
#include "private/state.hpp"
#include "proto/orders.pb.h"
#include "proto/processing.pb.h"
#include "proto/trades.pb.h"
#include "rpc-stubs/xayarpcclient.h"

#include <chrono>
#include <string>
#include <vector>

namespace democrit
{

class TestTradeManager;
class TradeManager;

/**
 * Wrapper class around a TradeState protocol buffer, which has logic
 * to extract some data from the raw proto (e.g. our role in the trade)
 * as well as perform updates based on new data from the counterparty.
 *
 * Instances of this class are used purely temporarily, to work with
 * the underlying protocol buffers from the global state.
 */
class Trade
{

private:

  /**
   * The chrono clock used for the trade start time and for checking the
   * time it is active and (perhaps) timing it out.  Since C++20,
   * std::system_time is guaranteed to have an epoch based on the UNIX epoch;
   * even before that, that is the de-facto standard.
   */
  using Clock = std::chrono::system_clock;

  /**
   * The trade manager instance we are using, which holds general stuff like
   * RPC connections.
   */
  const TradeManager& tm;

  /** The current user's account name.  */
  const std::string& account;

  /**
   * The actual data for this.  This references the instance inside the
   * global state, and the global state will be locked during the entire
   * time of using this instance.
   */
  proto::TradeState& pb;

  /**
   * True if pb is mutable (i.e. the instance is constructed from a non-const
   * proto::TradeState& reference).
   */
  const bool isMutable;

  explicit Trade (const TradeManager& t, const std::string& a,
                  const proto::TradeState& p)
    : tm(t), account(a),
      pb(const_cast<proto::TradeState&> (p)), isMutable(false)
  {}

  explicit Trade (const TradeManager& t, const std::string& a,
                  proto::TradeState& p)
    : tm(t), account(a),
      pb(p), isMutable(true)
  {}

  /**
   * Returns an ID that is used to identify the particular trade among
   * all active trades, e.g. when matching up with received messages.
   * This consists of the maker's account name and the maker's order ID.
   * Both maker and taker, if working correctly, will make sure that no two
   * trades will be active at the same time for the same order ID.
   */
  std::string GetIdentifier () const;

  /**
   * Returns the type of order this is from our point of view.  In other words,
   * ASK if we are selling, and BID if we are buying.
   */
  proto::Order::Type GetOrderType () const;

  /**
   * Returns the role we have in this trade (maker or taker).
   */
  proto::Trade::Role GetRole () const;

  /**
   * Returns the chrono time-point corresponding to the proto data.
   */
  Clock::time_point GetStartTime () const;

  /**
   * Fills in the "basic" data for a ProcessingMessage for this trade,
   * namely the counterparty and identifier.
   */
  void InitProcessingMessage (proto::ProcessingMessage& msg) const;

  /**
   * Sets the "taking_order" field in a processing message for this trade.
   * This is used by the taker after creating the trade locally.
   */
  void SetTakingOrder (proto::ProcessingMessage& msg) const;

  /**
   * Merges in seller data received from the counterparty with our state.
   */
  void MergeSellerData (const proto::SellerData& sd);

  /**
   * Checks if we are the seller and still need to get our addresses for
   * the seller data.  If that is the case, retrieves them and adds them to
   * our TradeState proto.
   */
  bool CreateSellerData ();

  friend class TestTradeManager;
  friend class TradeManager;

public:

  Trade () = delete;
  Trade (const Trade&) = delete;
  void operator= (const Trade&) = delete;

  /**
   * Returns true if the trade is finalised.  This means that it is either
   * abandoned or we have seen sufficient confirmations on either the trade
   * itself or a double spend to consider it "done".  When this returns true,
   * a trade may be archived.
   */
  bool IsFinalised () const;

  /**
   * Returns data of this trade in the Trade proto format, which is used
   * in the public, external interface of Democrit.
   */
  proto::Trade GetPublicInfo () const;

  /**
   * Returns true if the given ProcessingMessage is meant for this trade.
   */
  bool Matches (const proto::ProcessingMessage& msg) const;

  /**
   * Updates the state of this Trade based on a given incoming message (which
   * is assumed to match this trade already).
   */
  void HandleMessage (const proto::ProcessingMessage& msg);

  /**
   * Checks if it is "our turn" based on the current state; and if so,
   * returns true and fills in the reply to send to the counterparty.
   */
  bool HasReply (proto::ProcessingMessage& reply);

};

/**
 * This class is responsible for managing the list of trades of the current
 * user account.  It holds some general stuff needed for processing trades,
 * and takes care of constructing the Trade instances as needed to handle
 * certain operations (like extracting the public data for all trades
 * or updating them for incoming messages).
 */
class TradeManager
{

private:

  /** The global state we use to read our trades from.  */
  State& state;

  /** MyOrders instance used to look up, lock and unlock taken orders.  */
  MyOrders& myOrders;

  /**
   * RPC client for Xaya calls (e.g. to get seller addresses, check name
   * outputs or sign transactions).
   */
  RpcClient<XayaRpcClient>& xayaRpc;

  /**
   * Process all active trades and move those that are finalised to the
   * trade archive instead.
   */
  void ArchiveFinalisedTrades ();

  /**
   * Adds a new trade, based on one of our own orders being taken by
   * some counterparty.
   *
   * During normal operation (outside of unit tests), this is called
   * only internally from inside ProcessMessage.  Any further processing
   * (e.g. setting the seller data right away if the taker is the seller)
   * as well as returning our reply is handled inside ProcessMessage as well.
   */
  bool OrderTaken (const proto::Order& o, const Amount units,
                   const std::string& taker);

  friend class TestTradeManager;
  friend class Trade;

protected:

  /**
   * Returns the current time (based on Trade::Clock) as UNIX timestamp,
   * which is stored in the TradeState proto.  For testing this is mocked
   * to return a fake time instead.
   */
  virtual int64_t GetCurrentTime () const;

public:

  explicit TradeManager (State& s, MyOrders& mo, RpcClient<XayaRpcClient>& x)
    : state(s), myOrders(mo), xayaRpc(x)
  {}

  virtual ~TradeManager () = default;

  TradeManager () = delete;
  TradeManager (const TradeManager&) = delete;
  void operator= (const TradeManager&) = delete;

  /**
   * Returns the public data about all trades in our state.
   */
  std::vector<proto::Trade> GetTrades () const;

  /**
   * Adds a new trade, based on taking the given order (i.e. we are the
   * taker, and the order is from the counterparty).  Returns true on success,
   * and sets the message to be sent to the counterparty.
   */
  bool TakeOrder (const proto::Order& o, const Amount units,
                  proto::ProcessingMessage& msg);

  /**
   * Process a given message we have received via XMPP direct messaging.
   * The sender name has already been translated to Xaya (decoded from the
   * XMPP encoding) and filled into the message's counterparty field.
   *
   * This method returns true if we have a reply, in which case it is filled
   * in accordingly.
   */
  bool ProcessMessage (const proto::ProcessingMessage& msg,
                       proto::ProcessingMessage& reply);

};

} // namespace democrit

#endif // DEMOCRIT_TRADES_HPP
