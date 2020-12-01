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
    switch (pb.type ())
      {
      case proto::Order::ASK:
        res["type"] = "ask";
        break;
      case proto::Order::BID:
        res["type"] = "bid";
        break;
      default:
        LOG (FATAL) << "Invalid order type: " << static_cast<int> (pb.type ());
      }

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

} // namespace democrit
