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

#ifndef DEMOCRIT_MYORDERS_HPP
#define DEMOCRIT_MYORDERS_HPP

#include "private/intervaljob.hpp"
#include "private/state.hpp"
#include "proto/orders.pb.h"

#include <chrono>
#include <mutex>
#include <memory>

namespace democrit
{

/**
 * The orders owned by the local user.  This provides functions to
 * easily manage them (e.g. cancel by ID or add a new one) as exposed
 * through the RPC interface.  It takes care of broadcasting them
 * as needed both to keep them updated and also to prevent them from
 * timing out for others.
 */
class MyOrders
{

private:

  /** Global state instance, which holds the orders.  */
  State& state;

  /** The worker job to send refreshing broadcasts.  */
  std::unique_ptr<IntervalJob> refresher;

  /**
   * Starts (or restarts) the refresher thread with the given interval.
   */
  void StartRefresher (std::chrono::milliseconds intv);

  /**
   * Runs a single refresh iteration.
   */
  void RunRefresh ();

  /**
   * Internal implementation of GetOrders (returns all own orders),
   * which allows specifying whether to include locked orders or not.
   */
  proto::OrdersOfAccount InternalGetOrders (bool includeLocked) const;

protected:

  /**
   * Validates a given order for an account.  By default this just
   * returns true, but it can be overridden to add proper validation.
   * This is used when adding an order, and also when orders are refreshed
   * to weed out invalid ones.
   */
  virtual bool ValidateOrder (const std::string& account,
                              const proto::Order& o) const;

  /**
   * Subclasses can implement this method to be notified of needed
   * updates for the orders of the current account.  This is mostly used
   * to broadcast them via XMPP, but can be used directly in tests as
   * well.
   */
  virtual void UpdateOrders (const proto::OrdersOfAccount& ownOrders)
  {}

public:

  template <typename Rep, typename Period>
    explicit MyOrders (State& s, const std::chrono::duration<Rep, Period> intv)
    : state(s)
  {
    StartRefresher (intv);
  }

  virtual ~MyOrders () = default;

  MyOrders () = delete;
  MyOrders (const MyOrders&) = delete;
  void operator= (const MyOrders&) = delete;

  /**
   * Adds a new order to the list of own orders.  If we have a validator set,
   * checks it first.  Returns true if it was added, and false if not (because
   * it was invalid).
   */
  bool Add (proto::Order&& o);

  /**
   * Cancels (removes) the order with the given ID, if it exists.
   */
  void RemoveById (uint64_t id);

  /**
   * Tries to "lock" an order by ID.  If the order is not locked (and exists),
   * this returns true, locks the order and sets the output argument to the
   * order's value (including setting the account).  If the order does not
   * exist or is already locked, it returns false.
   *
   * Locked orders are not broadcast as available own orders to the network.
   * They are currently being taken by someone, but the trade has not been
   * finalised and can be cancelled immediately (i.e. we have not yet provided
   * our signatures).  This is used to avoid race conditions when taking
   * orders, while still not removing them permanently in case the trade
   * gets stalled immediately by the other party.
   */
  bool TryLock (uint64_t id, proto::Order& out);

  /**
   * Unlocks a previously locked order.  This makes it available again to
   * be taken by anyone, and makes us broadcast it.
   */
  void Unlock (uint64_t id);

  /**
   * Returns the current set of own orders.  This includes locked orders.
   */
  proto::OrdersOfAccount
  GetOrders () const
  {
    return InternalGetOrders (true);
  }

};

} // namespace democrit

#endif // DEMOCRIT_MYORDERS_HPP
