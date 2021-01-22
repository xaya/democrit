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

#include "config.h"

#include "rpc-stubs/nfrpcclient.h"

#include "assetspec.hpp"
#include "daemon.hpp"
#include "rpcserver.hpp"

#include <xayautil/uint256.hpp>

#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>

namespace
{

using democrit::Amount;
using democrit::Asset;

DEFINE_string (gsp_rpc_url, "",
               "URL at which the GSP's JSON-RPC interface is available");

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya's JSON-RPC interface is available");
DEFINE_string (dem_rpc_url, "",
               "URL at which the Democrit GSP's RPC interface is available");

DEFINE_int32 (rpc_port, 0,
              "the port at which Democrit's JSON-RPC server will be started");

DEFINE_string (account, "",
               "Xaya account name (without p/) of the local user");
DEFINE_string (jid, "", "JID for logging into the XMPP server");
DEFINE_string (password, "", "password for logging into XMPP");

DEFINE_string (room, "democrit-nf@muc.chat.xaya.io",
               "XMPP room for the order exchange");

/**
 * Exception thrown for usage errors (won't be logged).
 */
class UsageError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

/**
 * Converts a Democrit asset (which is a string separating minter and asset
 * by \n) to the non-fungible JSON representation.  Returns JSON null if
 * the string cannot be split (i.e. is certainly invalid).
 */
Json::Value
GetNfAsset (const Asset& str)
{
  const size_t sep = str.find ('\n');
  if (sep == std::string::npos)
    return Json::Value ();

  Json::Value res(Json::objectValue);
  res["m"] = str.substr (0, sep);
  res["a"] = str.substr (sep + 1);

  return res;
}

/**
 * The AssetSpec for the nonfungible GSP.
 */
class NfAssetSpec : public democrit::AssetSpec
{

private:

  /** The RPC client to use for GSP queries.  */
  NfRpcClient& gsp;

  /**
   * Mutex to synchronise access to the RPC client (which is not thread safe
   * by itself).
   */
  mutable std::mutex mutRpc;

public:

  explicit NfAssetSpec (NfRpcClient& g)
    : gsp(g)
  {}

  std::string
  GetGameId () const override
  {
    return "nf";
  }

  bool
  IsAsset (const Asset& asset) const override
  {
    const auto jsonAsset = GetNfAsset (asset);
    if (jsonAsset.isNull ())
      return false;

    Json::Value response;
    {
      std::lock_guard<std::mutex> lock(mutRpc);
      response = gsp.getassetdetails (jsonAsset);
    }
    CHECK (response.isMember ("data"));

    return !response["data"].isNull ();
  }

  bool
  CanSell (const std::string& name, const Asset& asset, const Amount n,
           xaya::uint256& hash) const override
  {
    const auto jsonAsset = GetNfAsset (asset);
    CHECK (jsonAsset.isObject ());

    Json::Value response;
    {
      std::lock_guard<std::mutex> lock(mutRpc);
      response = gsp.getbalance (jsonAsset, name);
    }
    CHECK (response.isObject ());

    const auto& balance = response["data"];
    CHECK (balance.isInt64 ());

    const auto& hashVal = response["blockhash"];
    CHECK (hashVal.isString ());
    CHECK (hash.FromHex (hashVal.asString ()));

    return n <= balance.asInt64 ();
  }

  bool
  CanBuy (const std::string& name, const Asset& asset,
          const Amount n) const override
  {
    return true;
  }

  Json::Value
  GetTransferMove (const std::string& sender, const std::string& receiver,
                   const Asset& asset, const Amount n) const override
  {
    const auto jsonAsset = GetNfAsset (asset);
    CHECK (jsonAsset.isObject ());

    Json::Value transfer(Json::objectValue);
    transfer["a"] = jsonAsset;
    transfer["n"] = static_cast<Json::Int64> (n);
    transfer["r"] = receiver;

    Json::Value res(Json::objectValue);
    res["t"] = transfer;
    return res;
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run Democrit for the nonfungible GSP");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  try
    {
      if (FLAGS_gsp_rpc_url.empty ())
        throw UsageError ("--gsp_rpc_url must be set");
      if (FLAGS_xaya_rpc_url.empty ())
        throw UsageError ("--xaya_rpc_url must be set");
      if (FLAGS_dem_rpc_url.empty ())
        throw UsageError ("--dem_rpc_url must be set");

      if (FLAGS_rpc_port == 0)
        throw UsageError ("--rpc_port must be set");

      if (FLAGS_account.empty ())
        throw UsageError ("--account must be set");
      if (FLAGS_jid.empty ())
        throw UsageError ("--jid must be set");

      jsonrpc::HttpClient httpGsp(FLAGS_gsp_rpc_url);
      NfRpcClient gsp(httpGsp);
      NfAssetSpec spec(gsp);

      democrit::Daemon daemon(spec, FLAGS_account,
                              FLAGS_xaya_rpc_url, FLAGS_dem_rpc_url,
                              FLAGS_jid, FLAGS_password, FLAGS_room);

      jsonrpc::HttpServer httpServer(FLAGS_rpc_port);
      httpServer.BindLocalhost ();
      democrit::RpcServer server(daemon, httpServer);

      LOG (INFO) << "Starting JSON-RPC interface on port " << FLAGS_rpc_port;
      server.Run ();

      return EXIT_SUCCESS;
    }
  catch (const UsageError& exc)
    {
      std::cerr << "Error: " << exc.what () << std::endl;
      return EXIT_FAILURE;
    }
  catch (const std::exception& exc)
    {
      LOG (ERROR) << exc.what ();
      std::cerr << "Error: " << exc.what () << std::endl;
      return EXIT_FAILURE;
    }
}
