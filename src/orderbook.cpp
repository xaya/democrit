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

#include "private/orderbook.hpp"

#include <glog/logging.h>

#include <algorithm>

namespace democrit
{

void
OrderBook::StartTimeouter ()
{
  timeouter = std::make_unique<IntervalJob> (timeoutIntv, [this] ()
    {
      RunTimeout ();
    });
}

void
OrderBook::RunTimeout ()
{
  VLOG (2) << "Running timeout tick...";

  std::lock_guard<std::mutex> lock(mut);
  const auto timeoutBefore = Clock::now () - timeout;

  while (!updates.empty () && updates.front ().time < timeoutBefore)
    {
      const std::string account = std::move (updates.front ().account);
      updates.pop ();

      auto mit = orders.find (account);
      if (mit == orders.end ())
        continue;

      if (mit->second.lastUpdate < timeoutBefore)
        {
          VLOG (1) << "Timing out orders of " << account;
          orders.erase (mit);
        }
    }
}

void
OrderBook::UpdateOrders (proto::OrdersOfAccount&& upd)
{
  CHECK (upd.has_account ());
  for (const auto& entry : upd.orders ())
    {
      const auto& o = entry.second;
      CHECK (o.has_asset () && o.has_type () && o.has_price_sat ());
    }

  const std::string account = std::move (*upd.mutable_account ());
  upd.clear_account ();

  std::lock_guard<std::mutex> lock(mut);
  const auto time = Clock::now ();

  if (upd.orders ().empty ())
    {
      VLOG (1) << "Deleting all orders of " << account;
      orders.erase (account);
      return;
    }

  VLOG (1) << "Updating orders of " << account;
  updates.emplace (account, time);
  orders[account] = AccountOrders (std::move (upd), time);
}

namespace
{

/**
 * Updates an OrderbookByAsset proto with the given order.  This adds in
 * the order as bid or ask into the matching asset entry.  The bids and
 * asks fields are not kept sorted for now.
 *
 * The order itself is moved into this function, and should already be a copy
 * made prior from the orders-by-account map, with account and id added in.
 */
void
AddOrderForAsset (proto::OrderbookByAsset& orders, proto::Order&& o)
{
  CHECK (o.has_asset ());

  proto::OrderbookForAsset* forAsset = nullptr;
  auto& assetMap = *orders.mutable_assets ();
  auto mit = assetMap.find (o.asset ());
  if (mit != assetMap.end ())
    forAsset = &mit->second;
  else
    {
      forAsset = &assetMap[o.asset ()];
      forAsset->set_asset (o.asset ());
    }
  o.clear_asset ();

  const auto type = o.type ();
  o.clear_type ();
  switch (type)
    {
    case proto::Order::ASK:
      forAsset->add_asks ()->Swap (&o);
      break;
    case proto::Order::BID:
      forAsset->add_bids ()->Swap (&o);
      break;
    default:
      LOG (FATAL) << "Unexpected order type: " << static_cast<int> (type);
    }
}

/**
 * Sorts all bids and asks in the order lists.  Ties are broken by
 * account and ID.
 */
void
SortByPrices (proto::OrderbookByAsset& orders)
{
  const auto byPriceAsc = [] (const proto::Order& a, const proto::Order& b)
    {
      CHECK (a.has_price_sat () && a.has_account () && a.has_id ());
      CHECK (b.has_price_sat () && b.has_account () && b.has_id ());

      if (a.price_sat () != b.price_sat ())
        return a.price_sat () < b.price_sat ();
      if (a.account () != b.account ())
        return a.account () < b.account ();
      return a.id () < b.id ();
    };

  for (auto& entry : *orders.mutable_assets ())
    {
      auto& asks = *entry.second.mutable_asks ();
      std::sort (asks.begin (), asks.end (), byPriceAsc);
      auto& bids = *entry.second.mutable_bids ();
      std::sort (bids.rbegin (), bids.rend (), byPriceAsc);
    }
}

} // anonymous namespace

proto::OrderbookByAsset
OrderBook::InternalGetByAsset (const Asset* asset) const
{
  proto::OrderbookByAsset res;
  for (const auto& accounts : orders)
    {
      const std::string& acc = accounts.first;
      for (const auto& order : accounts.second.orders.orders ())
        {
          CHECK (order.second.has_asset ());
          if (asset != nullptr && order.second.asset () != *asset)
            continue;

          proto::Order o = order.second;
          o.set_account (acc);
          o.set_id (order.first);
          AddOrderForAsset (res, std::move (o));
        }
    }

  SortByPrices (res);

  return res;
}

proto::OrderbookForAsset
OrderBook::GetForAsset (const Asset& asset) const
{
  auto allAssets = InternalGetByAsset (&asset);

  if (allAssets.assets ().empty ())
    {
      proto::OrderbookForAsset res;
      res.set_asset (asset);
      return res;
    }

  CHECK_EQ (allAssets.assets_size (), 1);
  auto mit = allAssets.mutable_assets ()->find (asset);
  CHECK (mit != allAssets.mutable_assets ()->end ());
  CHECK_EQ (mit->first, asset);
  CHECK_EQ (mit->second.asset (), asset);

  proto::OrderbookForAsset res;
  res.Swap (&mit->second);
  return res;
}

proto::OrderbookByAsset
OrderBook::GetByAsset () const
{
  return InternalGetByAsset (nullptr);
}

} // namespace democrit
