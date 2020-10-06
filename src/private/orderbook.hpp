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

#ifndef DEMOCRIT_ORDERBOOK_HPP
#define DEMOCRIT_ORDERBOOK_HPP

#include "assetspec.hpp"
#include "private/intervaljob.hpp"
#include "proto/orders.pb.h"

#include <chrono>
#include <map>
#include <mutex>
#include <memory>
#include <queue>
#include <string>

namespace democrit
{

/**
 * Handler of the orderbook.  This takes care of all known orders from
 * everyone in the system, handles updates to it from received broadcast
 * messages, and times out stale orders.  It does *not* handle our
 * own orders specifically.
 *
 * The class itself does not deal with any of the XMPP side of things, though.
 * It expects already Xaya account names and orders in the form of protocol
 * buffer messages, and exposes them in this form as well.
 *
 * Validation of received orders (with an AssetSpec) has to be done outside,
 * before passing the orders in here, if desired.
 *
 * This class does necessary synchronisation on its own.
 */
class OrderBook
{

private:

  /**
   * The maximum / default interval between runs of the timeout process.
   * This is long enough to not have any performance impact, and much
   * shorter than the default timeout itself, so that it will timely
   * remove old orders.
   */
  static constexpr auto MAX_TIMEOUT_INTV = std::chrono::seconds (5);

  /** The clock we use for timeouts.  */
  using Clock = std::chrono::steady_clock;

  /** Minimum age before an account's orders time out.  */
  const Clock::duration timeout;

  /**
   * The interval that we use for processing timeouts.  This is set to
   * a normal default value in the constructor, but can be overridden
   * for unit tests.
   */
  Clock::duration timeoutIntv;

  /**
   * The per-account data that we store for the orderbook.
   */
  struct AccountOrders
  {

    /** The actual orders of that account.  */
    proto::OrdersOfAccount orders;

    /** The last update time.  */
    Clock::time_point lastUpdate;

    explicit AccountOrders (proto::OrdersOfAccount&& o,
                            const Clock::time_point t)
      : orders(std::move (o)), lastUpdate(t)
    {}

    AccountOrders () = default;
    AccountOrders (AccountOrders&&) = default;
    AccountOrders& operator= (AccountOrders&&) = default;

    AccountOrders (const AccountOrders&) = delete;
    void operator= (const AccountOrders&) = delete;

  };

  /** Orders of all other accounts that we know of.  */
  std::map<std::string, AccountOrders> orders;

  /**
   * An entry into the queue of update events.
   */
  struct UpdateEvent
  {

    /** The account that was updated.  */
    std::string account;

    /** The update's time.  */
    Clock::time_point time;

    explicit UpdateEvent (const std::string& a, const Clock::time_point t)
      : account(a), time(t)
    {}

    UpdateEvent (UpdateEvent&&) = default;
    UpdateEvent (const UpdateEvent&) = delete;
    void operator= (const UpdateEvent&) = delete;

  };

  /**
   * A queue of all the order updates we have received.  When trying to
   * time out orders, we process the front elements of the queue, until
   * the timestamp is too fresh.  Note that if an account is updated
   * again, any previous entries remain in the queue (and will just be
   * ignored when timing out orders).
   */
  std::queue<UpdateEvent> updates;

  /** Lock used for this instance.  */
  std::mutex mut;

  /** The worker job to run timeouts.  */
  std::unique_ptr<IntervalJob> timeouter;

  /**
   * Starts the timeouter thread.
   */
  void StartTimeouter ();

  /**
   * Runs a single timeout iteration.
   */
  void RunTimeout ();

  /**
   * Internal implementation of GetByAsset, which allows filtering for
   * only one of the assets (ignoring all others).  This is used both
   * for GetForAsset and GetByAsset.  If asset is set as null, then all
   * assets will be returned instead.
   */
  proto::OrderbookByAsset InternalGetByAsset (const Asset* asset) const;

public:

  template <typename Rep, typename Period>
    explicit OrderBook (const std::chrono::duration<Rep, Period> to)
    : timeout(to), timeoutIntv(MAX_TIMEOUT_INTV)
  {
    /* If the timeout interval is longer than the actual timeout (because
       we set it to something very short in a test), set the timeout
       interval to the same value instead.  */
    if (timeoutIntv > timeout)
      timeoutIntv = timeout;

    StartTimeouter ();
  }

  OrderBook () = delete;
  OrderBook (const OrderBook&) = delete;
  void operator= (const OrderBook&) = delete;

  /**
   * Updates the orders of the given account in the database.  If there
   * are no orders specified, then the account will be removed from our
   * database instead.
   */
  void UpdateOrders (proto::OrdersOfAccount&& upd);

  /**
   * Returns the orderbook for a given asset (not including our
   * own orders if any).
   */
  proto::OrderbookForAsset GetForAsset (const Asset& asset) const;

  /**
   * Returns the entire orderbook (not including our own orders if any).
   */
  proto::OrderbookByAsset GetByAsset () const;

};

} // namespace democrit

#endif // DEMOCRIT_ORDERBOOK_HPP
