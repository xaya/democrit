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

#ifndef DEMOCRIT_TESTUTILS_HPP
#define DEMOCRIT_TESTUTILS_HPP

#include "assetspec.hpp"
#include "proto/orders.pb.h"
#include "proto/trades.pb.h"

#include <xayautil/uint256.hpp>

#include <gloox/jid.h>

#include <json/json.h>

#include <glog/logging.h>
#include <gmock/gmock.h>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <map>
#include <string>

namespace democrit
{

/**
 * Data for one of the test accounts that we use.
 */
struct TestAccount
{

  /** The username for the XMPP server.  */
  const char* name;

  /** The password for logging into the server.  */
  const char* password;

};

/**
 * Full set of "server configuration" used for testing.
 */
struct ServerConfiguration
{

  /** The XMPP server used.  */
  const char* server;

  /** The MUC service.  */
  const char* muc;

  /** The test accounts.  */
  TestAccount accounts[3];

};

/**
 * Returns the ServerConfiguration instance that should be used throughout
 * testing.
 *
 * This expects a local environment (with server running on localhost)
 * as it is set up e.g. by Charon's test/env Docker scripts.
 */
const ServerConfiguration& GetServerConfig ();

/**
 * Returns the path to the trusted CA file for the test server.
 */
std::string GetTestCA ();

/**
 * Returns the JID of the n-th test account from the selected server config.
 * Optinally adds a specified resource.
 */
gloox::JID GetTestJid (unsigned n, const std::string& res = "");

/**
 * Returns the password for the n-th test account.
 */
std::string GetPassword (unsigned n);

/**
 * Returns the full room JID (including server) to use in tests for
 * a given local room name.
 */
gloox::JID GetRoom (const std::string& nm);

/**
 * Sleeps some short amount of time, which we use to let the server process
 * some things in tests.
 */
void SleepSome ();

/**
 * Parses a string to JSON.
 */
Json::Value ParseJson (const std::string& str);

/**
 * Parses a protocol buffer from text format.
 */
template <typename Proto>
  Proto
  ParseTextProto (const std::string& str)
{
  Proto res;
  CHECK (google::protobuf::TextFormat::ParseFromString (str, &res));
  return res;
}

#define DEFINE_PROTO_MATCHER(name, type) \
  MATCHER_P (name, str, "") \
  { \
    const auto expected = ParseTextProto<proto::type> (str);\
    if (google::protobuf::util::MessageDifferencer::Equals (arg, expected)) \
      return true; \
    *result_listener << "actual: " << arg.DebugString (); \
    return false; \
  }

DEFINE_PROTO_MATCHER (EqualsOrdersForAsset, OrderbookForAsset)
DEFINE_PROTO_MATCHER (EqualsOrdersByAsset, OrderbookByAsset)
DEFINE_PROTO_MATCHER (EqualsOrdersOfAccount, OrdersOfAccount)
DEFINE_PROTO_MATCHER (EqualsTradeState, TradeState)
DEFINE_PROTO_MATCHER (EqualsTrade, Trade)

/**
 * Very simple AssetSpec to be used in testing.  It defines three valid
 * assets, "silver", "gold" and "bronze".  It also keeps track of the balances
 * each account has in either (which also acts as initialisation for accounts
 * with zero balance).  Everyone can buy who has been initialised, and everyone
 * can sell up to their balance.
 */
class TestAssets : public AssetSpec
{

private:

  /** Balances of each account (which are themselves a map for the assets).  */
  std::map<std::string, std::map<Asset, Amount>> balances;

  /** Block hash returned for the state.  */
  xaya::uint256 currentHash;

public:

  static constexpr const char* GAME_ID = "test";

  TestAssets () = default;

  void
  SetBlock (const xaya::uint256& hash)
  {
    currentHash = hash;
  }

  void
  SetBalance (const std::string& name, const Asset& asset, const Amount n)
  {
    balances[name][asset] = n;
  }

  void
  InitialiseAccount (const std::string& name)
  {
    balances[name];
  }

  std::string GetGameId () const override;
  bool IsAsset (const Asset& asset) const override;
  bool CanSell (const std::string& name, const Asset& asset, Amount n,
                xaya::uint256& hash) const override;
  bool CanBuy (const std::string& name, const Asset& asset,
               Amount n) const override;
  Json::Value GetTransferMove (const std::string& sender,
                               const std::string& receiver,
                               const Asset& asset, Amount n) const override;

};

} // namespace democrit

#endif // DEMOCRIT_TESTUTILS_HPP
