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

#include <glog/logging.h>

namespace dem
{

void
RpcServer::stop ()
{
  LOG (INFO) << "RPC method called: stop";
  game.RequestStop ();
}

Json::Value
RpcServer::getcurrentstate ()
{
  LOG (INFO) << "RPC method called: getcurrentstate";
  return game.GetCurrentJsonState ();
}

Json::Value
RpcServer::getpendingstate ()
{
  LOG (INFO) << "RPC method called: getpendingstate";
  return game.GetPendingJsonState ();
}

Json::Value
RpcServer::checktrade (const std::string& btxid)
{
  LOG (INFO) << "RPC method called: checktrade " << btxid;
  const auto data = logic.CheckTrade (game, btxid);

  Json::Value res(Json::objectValue);
  switch (data.state)
    {
    case DemGame::TradeState::UNKNOWN:
      res["state"] = "unknown";
      break;
    case DemGame::TradeState::PENDING:
      res["state"] = "pending";
      break;
    case DemGame::TradeState::CONFIRMED:
      res["state"] = "confirmed";
      res["height"] = static_cast<Json::Int> (data.confirmationHeight);
      break;
    default:
      LOG (FATAL) << "Unexpected trade state";
    }

  return res;
}

} // namespace dem
