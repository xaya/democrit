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

#include "config.h"

#include "game.hpp"
#include "pending.hpp"
#include "rpcserver.hpp"

#include <xayagame/defaultmain.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>

namespace
{

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya Core's JSON-RPC interface is available");
DEFINE_int32 (game_rpc_port, 0,
              "the port at which the GSP's JSON-RPC server will be started"
              " (if non-zero)");

DEFINE_int32 (enable_pruning, -1,
              "if non-negative (including zero), old undo data will be pruned"
              " and only as many blocks as specified will be kept");

DEFINE_string (datadir, "",
               "base data directory for state data"
               " (will be extended by 'dem' and the chain)");

class InstanceFactory : public xaya::CustomisedInstanceFactory
{

private:

  /**
   * Reference to the PXLogic instance.  This is needed to construct the
   * RPC server.
   */
  dem::DemGame& logic;

public:

  explicit InstanceFactory (dem::DemGame& l)
    : logic(l)
  {}

  std::unique_ptr<xaya::RpcServerInterface>
  BuildRpcServer (xaya::Game& game,
                  jsonrpc::AbstractServerConnector& conn) override
  {
    std::unique_ptr<xaya::RpcServerInterface> res;
    res.reset (new xaya::WrappedRpcServer<dem::RpcServer> (game, logic, conn));
    return res;
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run the Democrit GSP");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_xaya_rpc_url.empty ())
    {
      std::cerr << "Error: --xaya_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }

  if (FLAGS_datadir.empty ())
    {
      std::cerr << "Error: --datadir must be specified" << std::endl;
      return EXIT_FAILURE;
    }

  xaya::GameDaemonConfiguration config;
  config.XayaRpcUrl = FLAGS_xaya_rpc_url;
  if (FLAGS_game_rpc_port != 0)
    {
      config.GameRpcServer = xaya::RpcServerType::HTTP;
      config.GameRpcPort = FLAGS_game_rpc_port;
    }
  config.EnablePruning = FLAGS_enable_pruning;
  config.DataDirectory = FLAGS_datadir;

  dem::DemGame logic;
  InstanceFactory instanceFact(logic);
  config.InstanceFactory = &instanceFact;

  dem::PendingMoves pending;
  config.PendingMoves = &pending;

  return xaya::SQLiteMain (config, "dem", logic);
}
