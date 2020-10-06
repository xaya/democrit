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

  UpdateOrders (GetOrders ());
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

proto::OrdersOfAccount
MyOrders::GetOrders () const
{
  proto::OrdersOfAccount res;
  state.ReadState ([&res] (const proto::State& s)
    {
      res = s.own_orders ();
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
