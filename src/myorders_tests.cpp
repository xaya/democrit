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

#include "private/myorders.hpp"

#include "private/state.hpp"
#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

namespace democrit
{
namespace
{

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

protected:

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

};

class MyOrdersTests : public testing::Test
{

protected:

  State state;

  MyOrdersTests ()
  {
    state.AccessState ([] (proto::State& s)
      {
        s.set_account ("domob");
        s.set_next_free_id (101);
      });
  }

  /**
   * Calls AddOrder with data given as text proto.
   */
  static void
  AddOrder (MyOrders& mo, const std::string& str)
  {
    mo.Add (ParseTextProto<proto::Order> (str));
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

  mo.RemoveById (102);
  EXPECT_THAT (mo.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "domob"
    orders:
      {
        key: 101
        value: { asset: "gold" type: BID price_sat: 10 }
      }
  )"));

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

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
