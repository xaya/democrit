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

#include "daemon.hpp"

#include "mockxaya.hpp"
#include "private/authenticator.hpp"
#include "private/mucclient.hpp"
#include "private/stanzas.hpp"
#include "testutils.hpp"

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace democrit
{

DECLARE_string (democrit_xid_servers);
DECLARE_int64 (democrit_order_timeout_ms);

namespace
{

/** Order timeout used in tests.  */
constexpr auto TIMEOUT = std::chrono::milliseconds (100);

/* ************************************************************************** */

/**
 * Test case for Daemon.  It handles fudging with the command-line arguments
 * for trusted servers and the order timeout to adjust them to the values
 * we need for the test to work fine.
 */
class DaemonTests : public testing::Test
{

protected:

  TestAssets assets;
  TestEnvironment<MockXayaRpcServer> env;

  DaemonTests ()
  {
    FLAGS_democrit_xid_servers = GetServerConfig ().server;

    const std::chrono::milliseconds timeoutMs(TIMEOUT);
    FLAGS_democrit_order_timeout_ms = timeoutMs.count ();
  }

};

/**
 * Daemon instance that is based on a test XMPP account.
 */
class TestDaemon : public Daemon
{

private:

  /**
   * Gets the Xaya account name to use for the n-th test account.
   */
  static std::string
  GetTestAccount (const unsigned n)
  {
    Authenticator auth;

    std::string res;
    CHECK (auth.Authenticate (GetTestJid (n), res));

    return res;
  }

public:

  /**
   * Constructs the instance based on the n-th test account.
   */
  explicit TestDaemon (const AssetSpec& spec,
                       TestEnvironment<MockXayaRpcServer>& env,
                       const unsigned n)
    : Daemon(spec, GetTestAccount (n),
             env.GetXayaEndpoint (), env.GetGspEndpoint (),
             GetTestJid (n).full (), GetPassword (n), GetRoom ("room").full ())
  {
    CHECK (IsConnected ());
  }

  /**
   * Adds a new order to the daemon's own orders from the given text
   * proto.
   */
  void
  AddFromText (const std::string& str)
  {
    AddOrder (ParseTextProto<proto::Order> (str));
  }

};

/**
 * MUC client to connect with a test account and broadcast orders directly.
 * We use this to test situations that the Daemon itself doesn't allow, e.g.
 * sending a wrong account or not refreshing before timeout.
 */
class DirectOrderSender : public MucClient
{

public:

  explicit DirectOrderSender (const unsigned n)
    : MucClient(GetTestJid (n), GetPassword (n), GetRoom ("room"))
  {
    Connect ();
  }

  /**
   * Sends an order update from text proto.
   */
  void
  SendOrders (const std::string& str)
  {
    const auto orders = ParseTextProto<proto::OrdersOfAccount> (str);

    ExtensionData ext;
    ext.push_back (std::make_unique<AccountOrdersStanza> (orders));
    PublishMessage (std::move (ext));
  }

};

/* ************************************************************************** */

TEST_F (DaemonTests, Basic)
{
  TestDaemon d1(assets, env, 0), d2(assets, env, 1);
  auto d3 = std::make_unique<TestDaemon> (assets, env, 2);

  assets.SetBalance ("xmpptest1", "gold", 100);
  assets.InitialiseAccount ("xmpptest2");
  assets.SetBalance ("xmpptest3", "gold", 1);

  d1.AddFromText (R"(
    # ID and account will be ignored in here.
    account: "foo" id: 42
    asset: "gold" type: BID price_sat: 10 max_units: 1
  )");
  d1.AddFromText (R"(
    asset: "gold" type: ASK price_sat: 50 max_units: 1
  )");
  d2.AddFromText (R"(
    asset: "gold" type: BID price_sat: 5 max_units: 1
  )");

  SleepSome ();
  EXPECT_THAT (d1.GetOwnOrders (), EqualsOrdersOfAccount (R"(
    account: "xmpptest1"
    orders:
      {
        key: 0
        value: { asset: "gold" type: BID price_sat: 10 max_units: 1 }
      }
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 50 max_units: 1 }
      }
  )"));
  EXPECT_THAT (d2.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest1" id: 0 price_sat: 10 max_units: 1 }
    asks: { account: "xmpptest1" id: 1 price_sat: 50 max_units: 1 }
  )"));
  EXPECT_THAT (d3->GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest1" id: 0 price_sat: 10 max_units: 1 }
    bids: { account: "xmpptest2" id: 0 price_sat: 5 max_units: 1 }
    asks: { account: "xmpptest1" id: 1 price_sat: 50 max_units: 1 }
  )"));

  d1.CancelOrder (1);
  d3->AddFromText (R"(
    asset: "gold" type: ASK price_sat: 20 max_units: 1
  )");
  SleepSome ();
  EXPECT_THAT (d2.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest1" id: 0 price_sat: 10 max_units: 1 }
    asks: { account: "xmpptest3" id: 0 price_sat: 20 max_units: 1 }
  )"));

  d3.reset ();
  SleepSome ();
  EXPECT_THAT (d2.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest1" id: 0 price_sat: 10 max_units: 1 }
  )"));
}

TEST_F (DaemonTests, WrongAccountSent)
{
  TestDaemon d(assets, env, 0);
  DirectOrderSender sender(1);

  assets.InitialiseAccount ("xmpptest2");

  sender.SendOrders (R"(
    account: "foo"
    orders:
      {
        key: 0
        value: { asset: "gold" type: BID price_sat: 10 max_units: 1 }
      }
  )");

  SleepSome ();
  EXPECT_THAT (d.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest2" id: 0 price_sat: 10 max_units: 1 }
  )"));
}

TEST_F (DaemonTests, Timeout)
{
  TestDaemon d1(assets, env, 0), d2(assets, env, 1);
  DirectOrderSender sender(2);

  assets.SetBalance ("xmpptest1", "gold", 10);
  assets.InitialiseAccount ("xmpptest3");

  sender.SendOrders (R"(
    orders:
      {
        key: 0
        value: { asset: "gold" type: BID price_sat: 10 max_units: 1 }
      }
  )");
  d1.AddFromText (R"(
    asset: "gold" type: ASK price_sat: 50 max_units: 1
  )");

  SleepSome ();
  EXPECT_THAT (d2.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "xmpptest3" id: 0 price_sat: 10 max_units: 1 }
    asks: { account: "xmpptest1" id: 0 price_sat: 50 max_units: 1 }
  )"));

  std::this_thread::sleep_for (3 * TIMEOUT);
  EXPECT_THAT (d2.GetOrdersForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    asks: { account: "xmpptest1" id: 0 price_sat: 50 max_units: 1 }
  )"));
}

TEST_F (DaemonTests, OrderValidation)
{
  TestDaemon d(assets, env, 0);
  DirectOrderSender sender(1);

  // xmpptest1 is not (yet) initialised
  assets.SetBalance ("xmpptest2", "gold", 10);

  d.AddFromText (R"(
    asset: "gold" type: BID price_sat: 10
  )");
  sender.SendOrders (R"(
    orders:
      {
        key: 0
        value: { asset: "gold" type: ASK price_sat: 10 max_units: 11 }
      }
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 20 max_units: 10 }
      }
    orders:
      {
        key: 2
        value: { asset: "invalid" type: BID price_sat: 1 max_units: 1 }
      }
    orders:
      {
        key: 3
        value: { asset: "silver" type: BID price_sat: 5 max_units: 100 }
      }
    orders:
      {
        key: 4
        value: { asset: "silver" type: BID price_sat: 1 }
      }
    orders:
      {
        key: 5
        value: { asset: "silver" type: BID max_units: 1 }
      }
    orders:
      {
        key: 6
        value: { asset: "silver" type: BID price_sat: 1 max_units: 0 }
      }
    orders:
      {
        key: 7
        value:
          {
            asset: "silver"
            type: BID
            price_sat: 1
            min_units: 3
            max_units: 2
          }
      }
  )");

  SleepSome ();
  EXPECT_THAT (d.GetOwnOrders (), EqualsOrdersOfAccount (R"(
    account: "xmpptest1"
  )"));
  EXPECT_THAT (d.GetOrdersByAsset (), EqualsOrdersByAsset (R"(
    assets:
      {
        key: "gold"
        value:
          {
            asset: "gold"
            asks: { account: "xmpptest2" id: 1 price_sat: 20 max_units: 10 }
          }
      }
    assets:
      {
        key: "silver"
        value:
          {
            asset: "silver"
            bids: { account: "xmpptest2" id: 3 price_sat: 5 max_units: 100 }
          }
      }
  )"));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
