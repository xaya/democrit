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

#include "daemon.hpp"

#include "private/authenticator.hpp"
#include "private/intervaljob.hpp"
#include "private/mucclient.hpp"
#include "private/myorders.hpp"
#include "private/orderbook.hpp"
#include "private/stanzas.hpp"
#include "private/state.hpp"
#include "proto/orders.pb.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>

namespace democrit
{

DEFINE_int64 (democrit_order_timeout_ms, 10 * 60 * 1'000,
              "Timeout (in milliseconds) of orders when not refreshed");
DEFINE_int64 (democrit_reconnect_ms, 10 * 1'000,
              "Interval (in milliseconds) for trying to reconnect to XMPP");

/* ************************************************************************** */

/**
 * Specific MyOrders class that we use in the Daemon.  It implements the
 * update method to broadcast onto the MUC channel.
 */
class Daemon::MyOrdersImpl : public MyOrders
{

private:

  Impl& impl;

protected:

  bool ValidateOrder (const std::string& account,
                      const proto::Order& o) const override;
  void UpdateOrders (const proto::OrdersOfAccount& ownOrders) override;

public:

  explicit MyOrdersImpl (Impl& i);

};

/**
 * Actual implementation of the Daemon main logic.  This is the class that
 * collects and combines all the different pieces.
 */
class Daemon::Impl : private MucClient
{

private:

  /** Asset spec used to validate orders.  */
  const AssetSpec& spec;

  /** The internal "global" state with thread-safe access.  */
  State state;

  /** Authenticator for JIDs to account names.  */
  Authenticator auth;

  /** MyOrders implementation used.  */
  MyOrdersImpl myOrders;

  /** General orderbook that we know of.  */
  OrderBook allOrders;

  /** Interval job for checking the connection and perhaps reconnecting.  */
  std::unique_ptr<IntervalJob> reconnecter;

  /**
   * Returns true if the given order seems valid for the given account,
   * according to the asset spec.
   */
  bool ValidateOrder (const std::string& account, const proto::Order& o) const;

  friend class Daemon;
  friend class MyOrdersImpl;

protected:

  void HandleMessage (const gloox::JID& sender,
                      const gloox::Stanza& msg) override;
  void HandleDisconnect (const gloox::JID& disconnected) override;

public:

  explicit Impl (const AssetSpec& s, const std::string& account,
                 const std::string& jid, const std::string& password,
                 const std::string& mucRoom);

  Impl () = delete;
  Impl (const Impl&) = delete;
  void operator= (const Impl&) = delete;

};

/* ************************************************************************** */

Daemon::MyOrdersImpl::MyOrdersImpl (Impl& i)
  : MyOrders(i.state,
             std::chrono::milliseconds (FLAGS_democrit_order_timeout_ms) / 2),
    impl(i)
{}

bool
Daemon::MyOrdersImpl::ValidateOrder (const std::string& account,
                                     const proto::Order& o) const
{
  return impl.ValidateOrder (account, o);
}

void
Daemon::MyOrdersImpl::UpdateOrders (const proto::OrdersOfAccount& ownOrders)
{
  impl.state.ReadState ([&] (const proto::State& s)
    {
      CHECK_EQ (ownOrders.account (), s.account ());
    });

  if (!impl.IsConnected ())
    {
      VLOG (1) << "Ignoring order refresh while not connected";
      return;
    }

  MucClient::ExtensionData ext;
  ext.push_back (std::make_unique<AccountOrdersStanza> (ownOrders));
  impl.PublishMessage (std::move (ext));
}

Daemon::Impl::Impl (const AssetSpec& s, const std::string& account,
                    const std::string& jid, const std::string& password,
                    const std::string& mucRoom)
  : MucClient (gloox::JID (jid), password, gloox::JID (mucRoom)),
    spec(s),
    myOrders(*this),
    allOrders(std::chrono::milliseconds (FLAGS_democrit_order_timeout_ms))
{
  state.AccessState ([&] (proto::State& s)
    {
      s.set_account (account);
    });

  std::string jidAccount;
  CHECK (auth.Authenticate (gloox::JID (jid), jidAccount))
      << "Failed to authenticate our own JID " << jid;
  CHECK_EQ (jidAccount, account)
      << "Our JID " << jid << " does not match claimed account " << account;

  RegisterExtension (std::make_unique<AccountOrdersStanza> ());

  /* We do periodic reconnects (later), but also a synchronous connect right now
     to make sure the daemon is connected on startup.  */
  Connect ();

  const std::chrono::milliseconds reconnectIntv(FLAGS_democrit_reconnect_ms);
  reconnecter = std::make_unique<IntervalJob> (reconnectIntv, [&] ()
    {
      if (!IsConnected ())
        Connect ();
    });
}

bool
Daemon::Impl::ValidateOrder (const std::string& account,
                             const proto::Order& o) const
{
  if (o.max_units () == 0)
    return false;

  if (o.has_min_units ())
    {
      if (o.min_units () == 0)
        return false;
      if (o.min_units () > o.max_units ())
        return false;
    }

  if (!o.has_price_sat ())
    return false;

  if (!spec.IsAsset (o.asset ()))
    return false;

  switch (o.type ())
    {
    case proto::Order::BID:
      return spec.CanBuy (account, o.asset (), o.max_units ());

    case proto::Order::ASK:
      xaya::uint256 dummy;
      return spec.CanSell (account, o.asset (), o.max_units (), dummy);

    default:
      LOG (FATAL) << "Unexpected order type: " << static_cast<int> (o.type ());
    }
}

void
Daemon::Impl::HandleMessage (const gloox::JID& sender, const gloox::Stanza& msg)
{
  std::string account;
  if (!auth.Authenticate (sender, account))
    {
      LOG (WARNING) << "Failed to get account for JID " << sender.full ();
      return;
    }

  const auto* ordersExt
      = msg.findExtension<AccountOrdersStanza> (AccountOrdersStanza::EXT_TYPE);
  if (ordersExt != nullptr && ordersExt->IsValid ())
    {
      proto::OrdersOfAccount orders;
      orders.set_account (account);

      for (const auto& o : ordersExt->GetData ().orders ())
        if (ValidateOrder (account, o.second))
          orders.mutable_orders ()->insert (o);
        else
          LOG (WARNING)
              << "Ignoring invalid order from " << account << "\n:"
              << o.second.DebugString ();

      allOrders.UpdateOrders (std::move (orders));
    }
}

void
Daemon::Impl::HandleDisconnect (const gloox::JID& disconnected)
{
  std::string account;
  if (!auth.Authenticate (disconnected, account))
    {
      LOG (WARNING) << "Failed to get account for JID " << disconnected.full ();
      return;
    }

  proto::OrdersOfAccount o;
  o.set_account (account);
  /* We leave the orders empty.  */
  allOrders.UpdateOrders (std::move (o));
}

/* ************************************************************************** */

Daemon::Daemon (const AssetSpec& spec, const std::string& account,
                const std::string& jid, const std::string& password,
                const std::string& mucRoom)
  : impl(std::make_unique<Impl> (spec, account, jid, password, mucRoom))
{}

Daemon::~Daemon () = default;

proto::OrderbookForAsset
Daemon::GetOrdersForAsset (const Asset& asset) const
{
  return impl->allOrders.GetForAsset (asset);
}

proto::OrderbookByAsset
Daemon::GetOrdersByAsset () const
{
  return impl->allOrders.GetByAsset ();
}

bool
Daemon::AddOrder (proto::Order&& o)
{
  return impl->myOrders.Add (std::move (o));
}

void
Daemon::CancelOrder (const uint64_t id)
{
  impl->myOrders.RemoveById (id);
}

proto::OrdersOfAccount
Daemon::GetOwnOrders () const
{
  return impl->myOrders.GetOrders ();
}

bool
Daemon::IsConnected () const
{
  return impl->IsConnected ();
}

/* ************************************************************************** */

} // namespace democrit
