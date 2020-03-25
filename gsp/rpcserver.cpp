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

std::string
RpcServer::checktrade (const std::string& name, const std::string& tradeId)
{
  LOG (INFO) << "RPC method called: checktrade " << name << " " << tradeId;
  switch (logic.CheckTrade (game, name, tradeId))
    {
    case DemGame::TradeState::UNKNOWN:
      return "unknown";
    case DemGame::TradeState::PENDING:
      return "pending";
    case DemGame::TradeState::CONFIRMED:
      return "confirmed";
    default:
      LOG (FATAL) << "Unexpected trade state";
    }
}

} // namespace dem
