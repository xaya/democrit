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

#include "private/myorders.hpp"

#include "private/state.hpp"
#include "testutils.hpp"

#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include <chrono>

namespace democrit
{
namespace
{

using google::protobuf::util::MessageDifferencer;

DEFINE_PROTO_MATCHER (EqualsOrder, Order)

/** Refresher interval used in (most) tests.  */
constexpr auto REFRESH_INTV = std::chrono::milliseconds (10);

/** Very long interval that effectively means "no refreshs".  */
constexpr auto NO_REFRESH = std::chrono::seconds (1'000);

/* ************************************************************************** */

/**
 * Instance of MyOrders used in tests.  It mostly keeps track of the
 * calls to UpdateOrders done so we can verify them.
 */
class TestMyOrders : public MyOrders
{

private:

  using Clock = std::chrono::steady_clock;

  /** Last ownOrders passed to UpdateOrders.  */
  proto::OrdersOfAccount lastOrders;

  /** Last time when UpdateOrders was called.  */
  Clock::time_point lastUpdate;

  /**
   * Assets that are considered "invalid" for order-validation purposes.
   * We use that to verify the order validation going on.
   */
  std::set<Asset> invalidAssets;

  /** Lock for invalidAssets.  */
  mutable std::mutex mutInvalidAssets;

protected:

  bool
  ValidateOrder (const std::string& account,
                 const proto::Order& o) const override
  {
    std::lock_guard<std::mutex> lock(mutInvalidAssets);
    return invalidAssets.count (o.asset ()) == 0;
  }

  void
  UpdateOrders (const proto::OrdersOfAccount& ownOrders) override
  {
    lastOrders = ownOrders;
    lastUpdate = Clock::now ();
  }

public:

  explicit TestMyOrders (State& s, const std::chrono::milliseconds intv)
    : MyOrders(s, intv)
  {}

  /**
   * Returns the difference in time to when the orders were
   * last updated.
   */
  Clock::duration
  GetUpdatedDuration () const
  {
    const auto res = Clock::now () - lastUpdate;
    CHECK (res >= res.zero ());
    return res;
  }

  /**
   * Returns the orders set in the last update.
   */
  const proto::OrdersOfAccount&
  GetLastOrders () const
  {
    return lastOrders;
  }

  /**
   * Compares the actual orders (per GetOrders) with the ones pushed
   * last via UpdateOrders and expects them to be equal.
   */
  void
  ExpectOrdersUpdated ()
  {
    ASSERT_TRUE (MessageDifferencer::Equals (GetLastOrders (), GetOrders ()));
  }

  /**
   * Marks an asset as invalid for order validation.
   */
  void
  AddInvalidAsset (const Asset& a)
  {
    std::lock_guard<std::mutex> lock(mutInvalidAssets);
    invalidAssets.insert (a);
  }

};

class MyOrdersTests : public testing::Test
{

protected:

  State state;

  MyOrdersTests ()
    : state("domob")
  {
    state.AccessState ([] (proto::State& s)
      {
        s.set_next_free_id (101);
      });
  }

  /**
   * Calls AddOrder with data given as text proto.
   */
  static bool
  AddOrder (MyOrders& mo, const std::string& str)
  {
    return mo.Add (ParseTextProto<proto::Order> (str));
  }

};

/* ************************************************************************** */

TEST_F (MyOrdersTests, Changes)
{
  TestMyOrders mo(state, NO_REFRESH);

  AddOrder (mo, R"(
    asset: "gold"
    type: BID
    price_sat: 10
  )");
  AddOrder (mo, R"(
    asset: "gold"
    type: ASK
    price_sat: 20
  )");
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
    orders:
      {
        key: 102
        value: { asset: "gold" type: ASK price_sat: 20 }
      }
  )"));
  mo.ExpectOrdersUpdated ();

  mo.RemoveById (102);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
  )"));
  mo.ExpectOrdersUpdated ();

  mo.RemoveById (42);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
  )"));

  mo.RemoveById (101);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
  )"));
}

TEST_F (MyOrdersTests, Refresh)
{
  TestMyOrders mo(state, REFRESH_INTV);

  std::this_thread::sleep_for (5 * REFRESH_INTV);
  EXPECT_LE (mo.GetUpdatedDuration (), REFRESH_INTV);

  AddOrder (mo, R"(
    asset: "gold"
    type: BID
    price_sat: 10
  )");
  std::this_thread::sleep_for (5 * REFRESH_INTV);
  EXPECT_LE (mo.GetUpdatedDuration (), REFRESH_INTV);
  EXPECT_THAT (mo.GetLastOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
  )"));

  mo.RemoveById (101);
  std::this_thread::sleep_for (5 * REFRESH_INTV);
  EXPECT_LE (mo.GetUpdatedDuration (), REFRESH_INTV);
  EXPECT_THAT (mo.GetLastOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
  )"));
}

TEST_F (MyOrdersTests, Validation)
{
  TestMyOrders mo(state, REFRESH_INTV);
  mo.AddInvalidAsset ("invalid");

  ASSERT_TRUE (AddOrder (mo, R"(
    asset: "not invalid"
    type: BID
    price_sat: 10
  )"));
  ASSERT_FALSE (AddOrder (mo, R"(
    asset: "invalid"
    type: ASK
    price_sat: 20
  )"));
  ASSERT_TRUE (AddOrder (mo, R"(
    asset: "later invalid"
    type: ASK
    price_sat: 30
  )"));
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "not invalid" type: BID price_sat: 10 }
      }
    orders:
      {
        key: 102
        value: { asset: "later invalid" type: ASK price_sat: 30 }
      }
  )"));

  mo.AddInvalidAsset ("later invalid");
  std::this_thread::sleep_for (3 * REFRESH_INTV);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "not invalid" type: BID price_sat: 10 }
      }
  )"));
}

TEST_F (MyOrdersTests, Locking)
{
  TestMyOrders mo(state, NO_REFRESH);

  AddOrder (mo, R"(
    asset: "gold"
    type: BID
    price_sat: 10
  )");
  AddOrder (mo, R"(
    asset: "gold"
    type: ASK
    price_sat: 20
  )");
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
    orders:
      {
        key: 102
        value: { asset: "gold" type: ASK price_sat: 20 }
      }
  )"));

  proto::Order o;
  ASSERT_TRUE (mo.TryLock (101, o));
  EXPECT_THAT (o, EqualsOrder (R"(
    account: "domob"
    id: 101
    asset: "gold"
    type: BID
    price_sat: 10
  )"));
  EXPECT_FALSE (mo.TryLock (101, o));
  EXPECT_FALSE (mo.TryLock (1'234, o));
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 locked: true }
      }
    orders:
      {
        key: 102
        value: { asset: "gold" type: ASK price_sat: 20 }
      }
  )"));
  EXPECT_THAT (mo.GetLastOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 102
        value: { asset: "gold" type: ASK price_sat: 20 }
      }
  )"));

  mo.Unlock (101);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
    orders:
      {
        key: 102
        value: { asset: "gold" type: ASK price_sat: 20 }
      }
  )"));
  mo.ExpectOrdersUpdated ();
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
