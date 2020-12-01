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

#include "private/myorders.hpp"

#include <glog/logging.h>

namespace democrit
{

void
MyOrders::StartRefresher (const std::chrono::milliseconds intv)
{
  refresher = std::make_unique<IntervalJob> (intv, [this] ()
    {
      RunRefresh ();
    });
}

void
MyOrders::RunRefresh ()
{
  VLOG (2) << "Refreshing set of own orders...";

  state.AccessState ([this] (proto::State& s)
    {
      proto::OrdersOfAccount updated;
      for (auto& o : *s.mutable_own_orders ()->mutable_orders ())
        {
          if (ValidateOrder (s.account (), o.second))
            updated.mutable_orders ()->insert ({o.first, std::move (o.second)});
          else
            LOG (WARNING)
                << "Dropping invalid own order:\n" << o.second.DebugString ();
        }
      s.mutable_own_orders ()->Swap (&updated);
    });

  UpdateOrders (InternalGetOrders (false));
}

bool
MyOrders::Add (proto::Order&& o)
{
  bool added = false;
  state.AccessState ([this, &o, &added] (proto::State& s)
    {
      if (!ValidateOrder (s.account (), o))
        {
          LOG (WARNING) << "Added order is invalid:\n" << o.DebugString ();
          return;
        }

      o.clear_account ();
      o.clear_id ();

      const auto id = s.next_free_id ();
      s.set_next_free_id (id + 1);

      VLOG (1)
          << "Adding new order with ID " << id << ":\n"
          << o.DebugString ();
      (*s.mutable_own_orders ()->mutable_orders ())[id].Swap (&o);
      added = true;
    });

  if (added)
    RunRefresh ();

  return added;
}

void
MyOrders::RemoveById (const uint64_t id)
{
  state.AccessState ([this, id] (proto::State& s)
    {
      VLOG (1) << "Removing order with ID " << id;
      s.mutable_own_orders ()->mutable_orders ()->erase (id);
    });

  RunRefresh ();
}

bool
MyOrders::TryLock (const uint64_t id, proto::Order& out)
{
  bool res = false;
  state.AccessState ([this, id, &res, &out] (proto::State& s)
    {
      auto mit = s.mutable_own_orders ()->mutable_orders ()->find (id);
      if (mit == s.mutable_own_orders ()->mutable_orders ()->end ())
        {
          LOG (WARNING) << "Can't lock non-existing order with ID " << id;
          return;
        }

      if (mit->second.locked ())
        {
          LOG (WARNING) << "Order with ID " << id << " is already locked";
          return;
        }

      VLOG (1) << "Locking order with ID " << id;
      out = mit->second;
      out.set_account (s.account ());
      out.set_id (id);
      mit->second.set_locked (true);
      res = true;
      return;
    });

  if (res)
    RunRefresh ();
  return res;
}

void
MyOrders::Unlock (const uint64_t id)
{
  state.AccessState ([this, id] (proto::State& s)
    {
      auto mit = s.mutable_own_orders ()->mutable_orders ()->find (id);
      CHECK (mit != s.mutable_own_orders ()->mutable_orders ()->end ())
          << "Order with ID " << id << " doesn't exist";
      CHECK (mit->second.locked ()) << "Order " << id << " isn't locked";
      mit->second.clear_locked ();
    });

  RunRefresh ();
}

proto::OrdersOfAccount
MyOrders::InternalGetOrders (const bool includeLocked) const
{
  proto::OrdersOfAccount res;
  state.ReadState ([&res, includeLocked] (const proto::State& s)
    {
      for (const auto& entry : s.own_orders ().orders ())
        if (includeLocked || !entry.second.locked ())
          res.mutable_orders ()->insert (entry);
      res.set_account (s.account ());
    });

  return res;
}

bool
MyOrders::ValidateOrder (const std::string& account,
                         const proto::Order& o) const
{
  return true;
}

} // namespace democrit
