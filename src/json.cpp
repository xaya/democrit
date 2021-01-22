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

#include "json.hpp"

#include "proto/orders.pb.h"
#include "proto/trades.pb.h"

#include <glog/logging.h>

#include <google/protobuf/repeated_field.h>

namespace democrit
{

namespace
{

using google::protobuf::RepeatedPtrField;

/**
 * Converts an integer to JSON, making sure to do it with the proper
 * signed JSON int64 type.
 */
Json::Value
IntToJson (const int64_t val)
{
  return static_cast<Json::Int64> (val);
}

/**
 * Converts an order type enum value to a JSON value (string).
 */
Json::Value
OrderTypeToJson (const proto::Order::Type t)
{
  switch (t)
    {
    case proto::Order::ASK:
      return "ask";
    case proto::Order::BID:
      return "bid";
    default:
      LOG (FATAL) << "Invalid order type: " << t;
    }
}

} // anonymous namespace

template <>
  Json::Value
  ProtoToJson<proto::Order> (const proto::Order& pb)
{
  Json::Value res(Json::objectValue);

  if (pb.has_account ())
    res["account"] = pb.account ();
  if (pb.has_id ())
    res["id"] = IntToJson (pb.id ());

  if (pb.has_asset ())
    res["asset"] = pb.asset ();

  if (pb.has_min_units ())
    res["min_units"] = IntToJson (pb.min_units ());
  else
    res["min_units"] = IntToJson (1);

  CHECK_GE (pb.max_units (), std::max<int64_t> (1, pb.min_units ()));
  res["max_units"] = IntToJson (pb.max_units ());
  res["price_sat"] = IntToJson (pb.price_sat ());

  if (pb.has_type ())
    res["type"] = OrderTypeToJson (pb.type ());

  if (pb.locked ())
    res["locked"] = true;

  return res;
}

template <>
  bool
  ProtoFromJson<proto::Order> (const Json::Value& val, proto::Order& pb)
{
  pb.Clear ();

  if (!val.isObject ())
    return false;

  if (val.isMember ("account"))
    {
      if (!val["account"].isString ())
        return false;
      pb.set_account (val["account"].asString ());
    }

  if (val.isMember ("id"))
    {
      if (!val["id"].isUInt64 ())
        return false;
      pb.set_id (val["id"].asUInt64 ());
    }

  if (val.isMember ("asset"))
    {
      if (!val["asset"].isString ())
        return false;
      pb.set_asset (val["asset"].asString ());
    }

  if (val.isMember ("min_units"))
    {
      if (!val["min_units"].isUInt64 ())
        return false;
      pb.set_min_units (val["min_units"].asUInt64 ());
    }

  if (!val["max_units"].isUInt64 ())
    return false;
  pb.set_max_units (val["max_units"].asUInt64 ());

  if (!val["price_sat"].isUInt64 ())
    return false;
  pb.set_price_sat (val["price_sat"].asUInt64 ());

  if (val.isMember ("type"))
    {
      if (!val["type"].isString ())
        return false;
      const std::string type = val["type"].asString ();
      if (type == "bid")
        pb.set_type (proto::Order::BID);
      else if (type == "ask")
        pb.set_type (proto::Order::ASK);
      else
        return false;
    }

  return true;
}

template <>
  Json::Value
  ProtoToJson<proto::OrdersOfAccount> (const proto::OrdersOfAccount& pb)
{
  std::map<uint64_t, Json::Value> ordersById;
  for (const auto& entry : pb.orders ())
    {
      Json::Value cur = ProtoToJson (entry.second);
      cur.removeMember ("account");
      cur["id"] = IntToJson (entry.first);
      ordersById.emplace (entry.first, cur);
    }

  Json::Value orders(Json::arrayValue);
  for (const auto& entry : ordersById)
    orders.append (entry.second);

  Json::Value res(Json::objectValue);
  res["account"] = pb.account ();
  res["orders"] = orders;

  return res;
}

namespace
{

/**
 * Converts one side of an orderbook (bids or asks) to JSON.
 */
Json::Value
OrderbookSideToJson (const RepeatedPtrField<proto::Order>& orders)
{
  Json::Value res(Json::arrayValue);
  for (const auto& o : orders)
    {
      Json::Value cur = ProtoToJson (o);
      cur.removeMember ("asset");
      cur.removeMember ("type");
      res.append (cur);
    }

  return res;
}

} // anonymous namespace

template <>
  Json::Value
  ProtoToJson<proto::OrderbookForAsset> (const proto::OrderbookForAsset& pb)
{
  Json::Value res(Json::objectValue);
  res["asset"] = pb.asset ();
  res["bids"] = OrderbookSideToJson (pb.bids ());
  res["asks"] = OrderbookSideToJson (pb.asks ());

  return res;
}

template <>
  Json::Value
  ProtoToJson<proto::OrderbookByAsset> (const proto::OrderbookByAsset& pb)
{
  Json::Value res(Json::objectValue);
  for (const auto& entry : pb.assets ())
    {
      Json::Value cur = ProtoToJson (entry.second);
      cur["asset"] = entry.first;
      res[entry.first] = cur;
    }

  return res;
}

template <>
  Json::Value
  ProtoToJson<proto::Trade> (const proto::Trade& pb)
{
  Json::Value res(Json::objectValue);
  res["start_time"] = IntToJson (pb.start_time ());
  res["counterparty"] = pb.counterparty ();
  res["type"] = OrderTypeToJson (pb.type ());
  res["asset"] = pb.asset ();
  res["units"] = IntToJson (pb.units ());
  res["price_sat"] = IntToJson (pb.price_sat ());

  switch (pb.state ())
    {
    case proto::Trade::INITIATED:
      res["state"] = "initiated";
      break;
    case proto::Trade::PENDING:
      res["state"] = "pending";
      break;
    case proto::Trade::SUCCESS:
      res["state"] = "success";
      break;
    case proto::Trade::FAILED:
      res["state"] = "failed";
      break;
    case proto::Trade::ABANDONED:
      res["state"] = "abandoned";
      break;
    default:
      LOG (FATAL) << "Unexpected state: " << pb.state ();
    }

  switch (pb.role ())
    {
    case proto::Trade::MAKER:
      res["role"] = "maker";
      break;
    case proto::Trade::TAKER:
      res["role"] = "taker";
      break;
    default:
      LOG (FATAL) << "Unexpected role: " << pb.role ();
    }

  return res;
}

} // namespace democrit
