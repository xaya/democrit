/*
    Democrit - atomic trades for XAYA games
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#ifndef DEMOCRIT_RPCCLIENT_HPP
#define DEMOCRIT_RPCCLIENT_HPP

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>

#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace democrit
{

/**
 * Thin wrapper around a libjson-rpc-cpp JSON-RPC client, which makes
 * sure it is thread-safe by using a separate HTTP client instance for
 * each thread.
 */
template <typename T>
  class RpcClient
{

private:

  /** The JSON-RPC HTTP endpoint to use.  */
  const std::string endpoint;

  /** The JSON-RPC client version to use.  */
  const jsonrpc::clientVersion_t clientVersion;

  /** The HTTP client instances we use for our JSON-RPC clients per thread.  */
  std::map<std::thread::id, jsonrpc::HttpClient> httpClients;

  /** The actual JSON-RPC client instances per thread.  */
  std::map<std::thread::id, T> rpcClients;

  /** Mutex protecting the maps.  */
  std::mutex mut;

public:

  /**
   * Constructs a new RPC client with the given endpoint.  By default it will
   * be using the JSONRPC_CLIENT_V2 protocol; if legacy is set to true, it
   * will use V1 instead (needed for Xaya Core).
   */
  explicit RpcClient (const std::string& ep, const bool l = false)
    : endpoint(ep),
      clientVersion(l ? jsonrpc::JSONRPC_CLIENT_V1 : jsonrpc::JSONRPC_CLIENT_V2)
  {}

  RpcClient () = delete;
  RpcClient (const RpcClient<T>&) = delete;
  void operator= (const RpcClient<T>&) = delete;

  /**
   * Exposes the underlying libjson-rpc-cpp client to call methods on,
   * but making sure it is done in a thread-safe way.
   */
  T& operator* ();

  /**
   * Exposes the underlying RPC client like operator*.
   */
  T*
  operator-> ()
  {
    return &(this->operator* ());
  }

};

} // namespace democrit

#include "rpcclient.tpp"

#endif // DEMOCRIT_RPCCLIENT_HPP
