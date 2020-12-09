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

#ifndef DEMOCRIT_GSP_RPCSERVER_HPP
#define DEMOCRIT_GSP_RPCSERVER_HPP

#include "rpc-stubs/gsprpcserverstub.h"

#include "game.hpp"

#include <xayagame/game.hpp>

#include <json/json.h>
#include <jsonrpccpp/server.h>

namespace dem
{

/**
 * JSON-RPC server for the Democrit GSP.
 */
class RpcServer : public GspRpcServerStub
{

private:

  /** The underlying Game instance that manages everything.  */
  xaya::Game& game;

  /** The Democrit GSP implementation.  */
  DemGame& logic;

public:

  explicit RpcServer (xaya::Game& g, DemGame& l,
                      jsonrpc::AbstractServerConnector& conn)
    : GspRpcServerStub(conn), game(g), logic(l)
  {}

  void stop () override;
  Json::Value getcurrentstate () override;
  Json::Value getpendingstate () override;

  Json::Value checktrade (const std::string& btxid) override;

};

} // namespace dem

#endif // DEMOCRIT_GSP_RPCSERVER_HPP
