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

#ifndef DEMOCRIT_MOCKXAYA_HPP
#define DEMOCRIT_MOCKXAYA_HPP

#include "private/rpcclient.hpp"
#include "rpc-stubs/xayarpcclient.h"
#include "rpc-stubs/xayarpcserverstub.h"

#include <json/json.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gmock/gmock.h>

#include <string>

namespace democrit
{

/**
 * Utility method for generating server ports to be used.  It uses an
 * internal call counter to cycle through some range, which should be good
 * enough to find free ports even if more than one mock server are running
 * at the same time.
 */
int GetPortForMockServer ();

/**
 * Mock Xaya RPC server.  It implements the RPC methods needed to process
 * Democrit trades, but uses hard-coded data or simple fake logic for most
 * of the things.
 */
class MockXayaRpcServer : public XayaRpcServerStub
{

private:

  /** How many addresses have been created already.  */
  unsigned addrCount = 0;

public:

  explicit MockXayaRpcServer (jsonrpc::AbstractServerConnector& conn)
    : XayaRpcServerStub(conn)
  {}

  /**
   * The addresses returned are of the form "addr N", which N counting
   * how many have been created already.
   */
  std::string getnewaddress () override;

  /**
   * The name "p/invalid" is assumed to not exist and will throw.  For other
   * names starting with "p/", e.g. "p/nm", the method will return the outpoint
   * "nm txid:12" with the name filled into the txhash.
   */
  Json::Value name_show (const std::string& name) override;

};

/**
 * Test environment with a mock Xaya RPC server (that can be parametrised
 * using the template parameter).  It starts a real HTTP server with the
 * mock RPC as backend, and sets up an RPC client that tests can use.
 */
template <typename XayaServer>
  class TestEnvironment
{

private:

  /** Port for the Xaya RPC server.  */
  const int xayaPort;

  /** HTTP server used for the test.  */
  jsonrpc::HttpServer xayaHttpServer;

  /** The Xaya RPC server.  */
  XayaServer xayaRpcServer;

  /** The RPC client connected to our mock Xaya server.  */
  RpcClient<XayaRpcClient> xayaClient;

  /**
   * Returns the HTTP endpoint for the Xaya test server at the given port.
   */
  static std::string GetXayaEndpoint (int port);

public:

  TestEnvironment ();
  ~TestEnvironment ();

  /**
   * Exposes the Xaya RPC client for tests.
   */
  RpcClient<XayaRpcClient>&
  GetXayaRpc ()
  {
    return xayaClient;
  }

};

} // namespace democrit

#include "mockxaya.tpp"

#endif // DEMOCRIT_MOCKXAYA_HPP
