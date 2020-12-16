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

#include "private/rpcclient.hpp"

#include "rpc-stubs/testrpcclient.h"
#include "rpc-stubs/testrpcserverstub.h"

#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gtest/gtest.h>

#include <chrono>
#include <sstream>
#include <vector>

namespace democrit
{
namespace
{

class TestRpcServer : public TestRpcServerStub
{

public:

  explicit TestRpcServer (jsonrpc::AbstractServerConnector& conn)
    : TestRpcServerStub(conn)
  {}

  int
  echo (const int val) override
  {
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
    return val;
  }

};

class RpcClientTests : public testing::Test
{

private:

  /** Port used for our test RPC server.  */
  static constexpr int PORT = 29'837;

  /** The HTTP server used for testing.  */
  jsonrpc::HttpServer httpServer;

  /** The test RPC server.  */
  TestRpcServer rpcServer;

  /**
   * Returns the endpoint to use for the test server as string.
   */
  static std::string
  GetEndpoint ()
  {
    std::ostringstream out;
    out << "http://localhost:" << PORT;
    return out.str ();
  }

protected:

  /**
   * Number of server threads used.  This also corresponds to the number
   * of concurrent client threads we will run in tests.
   */
  static constexpr int SERVER_THREADS = 100;

  /** RPC client being tested.  */
  RpcClient<TestRpcClient> client;

  RpcClientTests ()
    : httpServer(PORT, "", "", SERVER_THREADS),
      rpcServer(httpServer),
      client(GetEndpoint ())
  {
    rpcServer.StartListening ();
  }

  ~RpcClientTests ()
  {
    rpcServer.StopListening ();
  }

};

TEST_F (RpcClientTests, SingleThread)
{
  EXPECT_EQ (client->echo (42), 42);
  EXPECT_EQ ((*client).echo (100), 100);
}

TEST_F (RpcClientTests, ManyThreads)
{
  constexpr int numThreads = SERVER_THREADS;
  constexpr int callsPerThread = 100;

  std::vector<std::thread> threads;
  for (int i = 0; i < numThreads; ++i)
    threads.emplace_back ([this, i] ()
      {
        for (int j = 0; j < callsPerThread; ++j)
          {
            const int val = i * callsPerThread + j;
            EXPECT_EQ (client->echo (val), val);
          }
      });

  for (auto& t : threads)
    t.join ();
}

} // anonymous namespace
} // namespace democrit
