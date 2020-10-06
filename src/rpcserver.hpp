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

#ifndef DEMOCRIT_RPCSERVER_HPP
#define DEMOCRIT_RPCSERVER_HPP

#include "daemon.hpp"
#include "rpc-stubs/daemonrpcserverstub.h"

#include <json/json.h>
#include <jsonrpccpp/server.h>

#include <condition_variable>
#include <mutex>

namespace democrit
{

/**
 * Generic RPC server implementation for a democrit daemon.
 */
class RpcServer : public DaemonRpcServerStub
{

private:

  /** The Daemon this is for.  */
  Daemon& daemon;

  /** Flag set to indicate the server should shut down.  */
  bool shouldStop;

  /** Mutex for the stop flag.  */
  std::mutex mutStop;

  /** Condition variable for signalling "should stop".  */
  std::condition_variable cvStop;

public:

  explicit RpcServer (Daemon& d, jsonrpc::AbstractServerConnector& conn)
    : DaemonRpcServerStub(conn), daemon(d)
  {}

  RpcServer () = delete;
  RpcServer (const RpcServer&) = delete;
  void operator= (const RpcServer&) = delete;

  /**
   * Starts the server and blocks until it gets shut down again.
   */
  void Run ();

  void stop () override;
  Json::Value getstatus () override;

  Json::Value getordersforasset (const std::string& asset) override;
  Json::Value getordersbyasset () override;

  Json::Value getownorders () override;
  bool addorder (const Json::Value& order) override;
  Json::Value cancelorder (int id) override;

};

} // namespace democrit

#endif // DEMOCRIT_RPCSERVER_HPP
