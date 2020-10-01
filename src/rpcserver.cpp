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

#include "rpcserver.hpp"

#include "json.hpp"
#include "proto/orders.pb.h"

#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>

namespace democrit
{

void
RpcServer::Run ()
{
  std::unique_lock<std::mutex> lock(mutStop);
  shouldStop = false;

  StartListening ();

  while (!shouldStop)
    cvStop.wait (lock);

  StopListening ();
}

void
RpcServer::stop ()
{
  LOG (INFO) << "RPC method called: stop";

  std::lock_guard<std::mutex> lock(mutStop);
  shouldStop = true;
  cvStop.notify_all ();
}

Json::Value
RpcServer::getstatus ()
{
  LOG (INFO) << "RPC method called: getstatus";

  Json::Value res(Json::objectValue);
  res["connected"] = daemon.IsConnected ();
  res["gameid"] = daemon.GetAssetSpec ().GetGameId ();
  res["account"] = daemon.GetAccount ();

  return res;
}

Json::Value
RpcServer::getordersforasset (const std::string& asset)
{
  LOG (INFO) << "RPC method called: getordersforasset " << asset;
  return ProtoToJson (daemon.GetOrdersForAsset (asset));
}

Json::Value
RpcServer::getordersbyasset ()
{
  LOG (INFO) << "RPC method called: getordersbyasset";
  return ProtoToJson (daemon.GetOrdersByAsset ());
}

Json::Value
RpcServer::getownorders ()
{
  LOG (INFO) << "RPC method called: getownorders";
  return ProtoToJson (daemon.GetOwnOrders ());
}

bool
RpcServer::addorder (const Json::Value& order)
{
  LOG (INFO) << "RPC method called: addorder\n" << order;

  proto::Order o;
  if (!ProtoFromJson (order, o))
    throw jsonrpc::JsonRpcException (jsonrpc::Errors::ERROR_RPC_INVALID_PARAMS,
                                     "invalid order");

  return daemon.AddOrder (std::move (o));
}

Json::Value
RpcServer::cancelorder (const int id)
{
  LOG (INFO) << "RPC method called: cancelorder " << id;
  daemon.CancelOrder (id);
  return Json::Value ();
}

} // namespace democrit
