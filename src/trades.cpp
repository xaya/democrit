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

#include "private/trades.hpp"

#include <glog/logging.h>

#include <sstream>

namespace democrit
{

/* ************************************************************************** */

std::string
Trade::GetIdentifier () const
{
  /* New lines are not valid inside Xaya names, so they can act as
     separator between maker name and order ID.  */

  std::ostringstream res;
  res << pb.order ().account () << '\n' << pb.order ().id ();

  return res.str ();
}

proto::Order::Type
Trade::GetOrderType () const
{
  const auto role = GetRole ();
  if (role == proto::Trade::MAKER)
    return pb.order ().type ();
  CHECK_EQ (role, proto::Trade::TAKER) << "Unexpected role: " << role;

  switch (pb.order ().type ())
    {
    case proto::Order::BID:
      return proto::Order::ASK;
    case proto::Order::ASK:
      return proto::Order::BID;
    default:
      LOG (FATAL) << "Unexpected order type: " << pb.order ().type ();
    }
}

proto::Trade::Role
Trade::GetRole () const
{
  return pb.order ().account () == account
      ? proto::Trade::MAKER
      : proto::Trade::TAKER;
}

Trade::Clock::time_point
Trade::GetStartTime () const
{
  return Clock::time_point (std::chrono::seconds (pb.start_time ()));
}

bool
Trade::IsFinalised () const
{
  if (!pb.has_state ())
    return false;

  switch (pb.state ())
    {
    case proto::Trade::ABANDONED:
    case proto::Trade::SUCCESS:
    case proto::Trade::FAILED:
      return true;

    default:
      return false;
    }
}

proto::Trade
Trade::GetPublicInfo () const
{
  proto::Trade res;
  res.set_state (pb.state ());
  res.set_start_time (pb.start_time ());
  res.set_counterparty (pb.counterparty ());
  res.set_type (GetOrderType ());
  res.set_asset (pb.order ().asset ());
  res.set_units (pb.units ());
  res.set_price_sat (pb.order ().price_sat ());
  res.set_role (GetRole ());
  return res;
}

/* ************************************************************************** */

std::vector<proto::Trade>
TradeManager::GetTrades () const
{
  std::vector<proto::Trade> res;
  state.ReadState ([this, &res] (const proto::State& s)
    {
      for (const auto& t : s.trades ())
        res.push_back (Trade (*this, s.account (), t).GetPublicInfo ());
    });

  return res;
}

int64_t
TradeManager::GetCurrentTime () const
{
  const auto dur = Trade::Clock::now ().time_since_epoch ();
  return std::chrono::duration_cast<std::chrono::seconds> (dur).count ();
}

namespace
{

/**
 * Checks if the given order can be taken with the given amount,
 * and that it has in general all the fields necessary and is valid
 * for our purposes (so we can start a trade).
 */
bool
CheckOrder (const proto::Order& o, const Amount units)
{
  if (units > static_cast<Amount> (o.max_units ())
        || units < static_cast<Amount> (o.min_units ()))
    {
      LOG (WARNING)
          << "Cannot take order for " << units << " units:\n"
          << o.DebugString ();
      return false;
    }

  if (!o.has_account () || !o.has_id ()
        || !o.has_asset () || !o.has_type () || !o.has_price_sat ())
    {
      LOG (WARNING) << "Order to take is missing fields:\n" << o.DebugString ();
      return false;
    }

  return true;
}

} // anonymous namespace

bool
TradeManager::TakeOrder (const proto::Order& o, const Amount units)
{
  if (!CheckOrder (o, units))
    return false;

  proto::TradeState data;
  *data.mutable_order () = o;
  data.set_start_time (GetCurrentTime ());
  data.set_units (units);
  data.set_counterparty (o.account ());
  data.set_state (proto::Trade::INITIATED);

  bool ok;
  state.AccessState ([&data, &ok] (proto::State& s)
    {
      if (data.counterparty () == s.account ())
        {
          LOG (WARNING)
              << "Can't take own order:\n" << data.order ().DebugString ();
          ok = false;
        }
      else
        {
          *s.mutable_trades ()->Add () = std::move (data);
          ok = true;
        }
    });

  return ok;
}

bool
TradeManager::OrderTaken (const proto::Order& o, const Amount units,
                          const std::string& counterparty)
{
  if (!CheckOrder (o, units))
    return false;

  proto::TradeState data;
  *data.mutable_order () = o;
  data.set_start_time (GetCurrentTime ());
  data.set_units (units);
  data.set_counterparty (counterparty);
  data.set_state (proto::Trade::INITIATED);

  bool ok;
  state.AccessState ([&data, &ok] (proto::State& s)
    {
      CHECK_EQ (data.order ().account (), s.account ());

      if (data.counterparty () == s.account ())
        {
          LOG (WARNING)
              << "Order taken by ourselves:\n" << data.order ().DebugString ();
          ok = false;
        }
      else
        {
          *s.mutable_trades ()->Add () = std::move (data);
          ok = true;
        }
    });

  return ok;
}

/* ************************************************************************** */

} // namespace democrit
