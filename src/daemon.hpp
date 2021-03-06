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

#ifndef DEMOCRIT_DAEMON_HPP
#define DEMOCRIT_DAEMON_HPP

#include "assetspec.hpp"
#include "proto/orders.pb.h"
#include "proto/trades.pb.h"

#include <memory>
#include <string>

namespace democrit
{

class State;

/**
 * The main class for running a Democrit daemon.  It manages all the things
 * needed for it, like the underlying XMPP client connection, the processes
 * to listen to and update the order book and broadcast our own orders
 * regularly, and the handler of ongoing one-to-one trade negotiations.
 */
class Daemon
{

private:

  class Impl;
  class MyOrdersImpl;

  /**
   * The actual implementation, whose definition is hidden in the .cpp
   * file to decouple the public interface from internal stuff.
   */
  std::unique_ptr<Impl> impl;

  /**
   * Returns the internal state class held by the daemon instance.  This is
   * used in tests.
   */
  State& GetStateForTesting ();

  friend class TestDaemon;

public:

  explicit Daemon (const AssetSpec& spec, const std::string& account,
                   const std::string& xayaRpc, const std::string& demGspRpc,
                   const std::string& jid, const std::string& password,
                   const std::string& mucRoom);

  ~Daemon ();

  Daemon () = delete;
  Daemon (const Daemon&) = delete;
  void operator= (const Daemon&) = delete;

  /**
   * Sets the trusted root certificate for the XMPP server connection.
   * This must be called before Connect.
   */
  void SetRootCA (const std::string& path);

  /**
   * Connects the instance to the XMPP server, and also starts a background
   * thread that tries to reconnect it periodically.  The method itself
   * does not block, but returns after the initial connection is established.
   */
  void Connect ();

  /**
   * Returns the known orderbook (not including our own orders) for
   * a given asset.
   */
  proto::OrderbookForAsset GetOrdersForAsset (const Asset& asset) const;

  /**
   * Returns the entire orderbook (excluding our own orders) for
   * all assets that we know about.
   */
  proto::OrderbookByAsset GetOrdersByAsset () const;

  /**
   * Adds a new order to the list of own orders.  Returns false if the
   * given order seems invalid for our account.
   */
  bool AddOrder (proto::Order&& o);

  /**
   * Cancels an order (of the user's own) by ID.
   */
  void CancelOrder (uint64_t id);

  /**
   * Returns the own orders currently being advertised.
   */
  proto::OrdersOfAccount GetOwnOrders () const;

  /**
   * Returns the list of known trades.
   */
  std::vector<proto::Trade> GetTrades () const;

  /**
   * Requests to take another's order for the given number of units.
   * Returns true on success (if the process could at least be started)
   * and false if something failed right away.
   */
  bool TakeOrder (const proto::Order& o, Amount units);

  /**
   * Returns the account name this is running for.
   */
  std::string GetAccount () const;

  /**
   * Returns the AssetSpec used.
   */
  const AssetSpec& GetAssetSpec () const;

  /**
   * Returns true if the client is currently connected to the XMPP network.
   * It will try to reconnect periodically, but this can be used to give
   * status information for the "current state".
   */
  bool IsConnected () const;

};

} // namespace democrit

#endif // DEMOCRIT_DAEMON_HPP
