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

#include "private/trades.hpp"

#include "private/myorders.hpp"
#include "private/state.hpp"
#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace democrit
{

namespace
{

using testing::ElementsAre;

DEFINE_PROTO_MATCHER (EqualsTradeState, TradeState)
DEFINE_PROTO_MATCHER (EqualsTrade, Trade)

constexpr auto NO_EXPIRY = std::chrono::seconds (1'000);

} // anonymous namespace

/* ************************************************************************** */

/**
 * TradeManager instance used in tests.  It holds a State instance and
 * some fake data / mock RPC connections.  "me" is the account name
 * used for the current user.
 */
class TestTradeManager : public State, public MyOrders, public TradeManager
{

private:

  /** The mock time to return as "current" for created trades.  */
  int64_t mockTime;

  int64_t
  GetCurrentTime () const override
  {
    return mockTime;
  }

public:

  TestTradeManager ()
    : State("me"),
      MyOrders(static_cast<State&> (*this), NO_EXPIRY),
      TradeManager(static_cast<State&> (*this), static_cast<MyOrders&> (*this)),
      mockTime(0)
  {}

  void
  SetMockTime (const int64_t t)
  {
    mockTime = t;
  }

  /**
   * Constructs a new Trade instance, based on the given data parsed
   * as text-format TradeState.
   */
  std::unique_ptr<Trade>
  GetTrade (const std::string& data)
  {
    auto pb = ParseTextProto<proto::TradeState> (data);
    proto::TradeState* ref;
    AccessState ([&pb, &ref] (proto::State& s)
      {
        s.clear_trades ();
        ref = s.mutable_trades ()->Add ();
        *ref = std::move (pb);
      });

    static const std::string me = "me";
    return std::unique_ptr<Trade> (new Trade (*this, me, *ref));
  }

  /**
   * Adds a new Trade to the internal state, based on the given proto data.
   */
  void
  AddTrade (const std::string& data)
  {
    auto pb = ParseTextProto<proto::TradeState> (data);
    AccessState ([&pb] (proto::State& s)
      {
        *s.mutable_trades ()->Add () = std::move (pb);
      });
  }

  using TradeManager::ArchiveFinalisedTrades;
  using TradeManager::OrderTaken;

};

namespace
{

/* ************************************************************************** */

class TradeStateTests : public testing::Test
{

protected:

  TestTradeManager tm;

  /**
   * Returns the public info data (Trade::GetPublicInfo) for the given
   * TradeState from text proto.
   */
  proto::Trade
  GetPublicInfo (const std::string& data)
  {
    return tm.GetTrade (data)->GetPublicInfo ();
  }

};

TEST_F (TradeStateTests, MakerBuyer)
{
  EXPECT_THAT (GetPublicInfo (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 100
        type: BID
      }
    units: 42
    counterparty: "other"
  )"), EqualsTrade (R"(
    state: INITIATED
    start_time: 123
    counterparty: "other"
    role: MAKER
    type: BID
    asset: "gold"
    units: 42
    price_sat: 100
  )"));
}

TEST_F (TradeStateTests, MakerSeller)
{
  EXPECT_THAT (GetPublicInfo (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 100
        type: ASK
      }
    units: 42
    counterparty: "other"
  )"), EqualsTrade (R"(
    state: INITIATED
    start_time: 123
    counterparty: "other"
    role: MAKER
    type: ASK
    asset: "gold"
    units: 42
    price_sat: 100
  )"));
}

TEST_F (TradeStateTests, TakerBuyer)
{
  EXPECT_THAT (GetPublicInfo (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "other"
        asset: "gold"
        price_sat: 100
        type: ASK
      }
    units: 42
    counterparty: "other"
  )"), EqualsTrade (R"(
    state: INITIATED
    start_time: 123
    counterparty: "other"
    role: TAKER
    type: BID
    asset: "gold"
    units: 42
    price_sat: 100
  )"));
}

TEST_F (TradeStateTests, TakerSeller)
{
  EXPECT_THAT (GetPublicInfo (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "other"
        asset: "gold"
        price_sat: 100
        type: BID
      }
    units: 42
    counterparty: "other"
  )"), EqualsTrade (R"(
    state: INITIATED
    start_time: 123
    counterparty: "other"
    role: TAKER
    type: ASK
    asset: "gold"
    units: 42
    price_sat: 100
  )"));
}

TEST_F (TradeStateTests, IsFinalised)
{
  EXPECT_TRUE (tm.GetTrade (R"(
    state: ABANDONED
  )")->IsFinalised ());
  EXPECT_TRUE (tm.GetTrade (R"(
    state: SUCCESS
  )")->IsFinalised ());
  EXPECT_TRUE (tm.GetTrade (R"(
    state: FAILED
  )")->IsFinalised ());
}

/* ************************************************************************** */

class TradeManagerTests : public testing::Test
{

protected:

  TestTradeManager tm;

  TradeManagerTests ()
  {
    tm.SetMockTime (123);
  }

};

TEST_F (TradeManagerTests, OrderVerification)
{
  const auto o = ParseTextProto<proto::Order> (R"(
    account: "other"
    id: 42
    asset: "gold"
    min_units: 10
    max_units: 100
    price_sat: 42
    type: BID
  )");

  proto::ProcessingMessage msg;
  EXPECT_FALSE (tm.TakeOrder (o, 9, msg));
  EXPECT_FALSE (tm.TakeOrder (o, 101, msg));

  auto modified = o;
  modified.clear_account ();
  EXPECT_FALSE (tm.TakeOrder (modified, 10, msg));

  modified = o;
  modified.clear_id ();
  EXPECT_FALSE (tm.TakeOrder (modified, 10, msg));

  modified = o;
  modified.clear_asset ();
  EXPECT_FALSE (tm.TakeOrder (modified, 10, msg));

  modified = o;
  modified.clear_price_sat ();
  EXPECT_FALSE (tm.TakeOrder (modified, 10, msg));

  modified = o;
  modified.clear_type ();
  EXPECT_FALSE (tm.TakeOrder (modified, 10, msg));

  EXPECT_THAT (tm.GetTrades (), ElementsAre ());
}

TEST_F (TradeManagerTests, TakingOwnOrder)
{
  const auto o = ParseTextProto<proto::Order> (R"(
    account: "me"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 42
    type: BID
  )");

  proto::ProcessingMessage msg;
  EXPECT_FALSE (tm.TakeOrder (o, 10, msg));
  EXPECT_FALSE (tm.OrderTaken (o, 10, "me"));

  EXPECT_THAT (tm.GetTrades (), ElementsAre ());
}

TEST_F (TradeManagerTests, TakingOrderSuccess)
{
  const auto own = ParseTextProto<proto::Order> (R"(
    account: "me"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 42
    type: BID
  )");
  const auto other = ParseTextProto<proto::Order> (R"(
    account: "other"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 64
    type: ASK
  )");

  proto::ProcessingMessage msg;
  ASSERT_TRUE (tm.OrderTaken (own, 100, "other"));
  ASSERT_TRUE (tm.TakeOrder (other, 10, msg));

  EXPECT_THAT (tm.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: INITIATED
      start_time: 123
      counterparty: "other"
      type: BID
      asset: "gold"
      units: 100
      price_sat: 42
      role: MAKER
    )"),
    EqualsTrade (R"(
      state: INITIATED
      start_time: 123
      counterparty: "other"
      type: BID
      asset: "gold"
      units: 10
      price_sat: 64
      role: TAKER
    )")
  ));
}

TEST_F (TradeManagerTests, Archive)
{
  tm.AddTrade (R"(
    state: INITIATED
    start_time: 1
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 100
        type: BID
      }
    units: 42
    counterparty: "other"
  )");
  tm.AddTrade (R"(
    state: ABANDONED
    start_time: 2
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 20
        type: BID
      }
    units: 100
    counterparty: "other"
  )");
  tm.AddTrade (R"(
    state: PENDING
    start_time: 3
    order:
      {
        account: "me"
        asset: "silver"
        price_sat: 20
        type: ASK
      }
    units: 1
    counterparty: "other"
    their_psbt: "partial"
    our_psbt: "signed"
  )");
  tm.AddTrade (R"(
    state: SUCCESS
    start_time: 4
    order:
      {
        account: "other"
        asset: "gold"
        price_sat: 42
        type: BID
      }
    units: 10
    counterparty: "other"
  )");
  tm.AddTrade (R"(
    state: FAILED
    start_time: 5
    order:
      {
        account: "other"
        asset: "gold"
        price_sat: 64
        type: BID
      }
    units: 5
    counterparty: "other"
  )");

  tm.ArchiveFinalisedTrades ();

  EXPECT_THAT (tm.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: INITIATED
      start_time: 1
      counterparty: "other"
      type: BID
      asset: "gold"
      units: 42
      price_sat: 100
      role: MAKER
    )"),
    EqualsTrade (R"(
      state: PENDING
      start_time: 3
      counterparty: "other"
      type: ASK
      asset: "silver"
      units: 1
      price_sat: 20
      role: MAKER
    )"),
    EqualsTrade (R"(
      state: ABANDONED
      start_time: 2
      counterparty: "other"
      type: BID
      asset: "gold"
      units: 100
      price_sat: 20
      role: MAKER
    )"),
    EqualsTrade (R"(
      state: SUCCESS
      start_time: 4
      counterparty: "other"
      type: ASK
      asset: "gold"
      units: 10
      price_sat: 42
      role: TAKER
    )"),
    EqualsTrade (R"(
      state: FAILED
      start_time: 5
      counterparty: "other"
      type: ASK
      asset: "gold"
      units: 5
      price_sat: 64
      role: TAKER
    )")
  ));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
