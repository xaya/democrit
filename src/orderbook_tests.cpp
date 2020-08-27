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

#include "private/orderbook.hpp"

#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <google/protobuf/util/message_differencer.h>

#include <chrono>

using google::protobuf::util::MessageDifferencer;

namespace democrit
{
namespace
{

/* ************************************************************************** */

class OrderbookTests : public testing::Test
{

protected:

  /**
   * Calls UpdateOrders with data given as text proto.
   */
  static void
  UpdateOrders (OrderBook& ob, const std::string& str)
  {
    ob.UpdateOrders (ParseTextProto<proto::OrdersOfAccount> (str));
  }

};

class OrderbookWithoutTimeout : public OrderBook
{

public:

  OrderbookWithoutTimeout ()
    : OrderBook(std::chrono::seconds (1'000))
  {}

};

MATCHER_P (EqualsOrdersForAsset, str, "")
{
  const auto expected = ParseTextProto<proto::OrderbookForAsset> (str);
  if (MessageDifferencer::Equals (arg, expected))
    return true;

  *result_listener << "actual: " << arg.DebugString ();
  return false;
}

MATCHER_P (EqualsOrdersByAsset, str, "")
{
  const auto expected = ParseTextProto<proto::OrderbookByAsset> (str);
  if (MessageDifferencer::Equals (arg, expected))
    return true;

  *result_listener << "actual: " << arg.DebugString ();
  return false;
}

/* ************************************************************************** */

TEST_F (OrderbookTests, EmptyBooks)
{
  OrderbookWithoutTimeout o;
  EXPECT_THAT (o.GetForAsset ("foo"), EqualsOrdersForAsset (R"(
    asset: "foo"
  )"));
  EXPECT_THAT (o.GetByAsset (), EqualsOrdersByAsset (""));
}

TEST_F (OrderbookTests, BookByAsset)
{
  OrderbookWithoutTimeout o;

  UpdateOrders (o, R"(
    account: "domob"
    orders:
      {
        key: 1
        value:
          {
            asset: "gold"
            min_units: 1
            max_units: 2
            type: ASK
            price_sat: 123
          }
      }
    orders:
      {
        key: 2
        value: { asset: "gold" type: BID price_sat: 50 }
      }
  )");

  UpdateOrders (o, R"(
    account: "andy"
    orders:
      {
        key: 10
        value: { asset: "gold" type: ASK price_sat: 100 }
      }
    orders:
      {
        key: 20
        value: { asset: "gold" type: BID price_sat: 2 }
      }
    orders:
      {
        key: 30
        value: { asset: "silver" type: BID price_sat: 1 }
      }
  )");

  EXPECT_THAT (o.GetForAsset ("foo"), EqualsOrdersForAsset (R"(
    asset: "foo"
  )"));

  EXPECT_THAT (o.GetForAsset ("silver"), EqualsOrdersForAsset (R"(
    asset: "silver"
    bids: { account: "andy" id: 30 price_sat: 1 }
  )"));

  EXPECT_THAT (o.GetByAsset (), EqualsOrdersByAsset (R"(
    assets:
      {
        key: "gold"
        value:
          {
            asset: "gold"
            bids: { account: "domob" id: 2 price_sat: 50 }
            bids: { account: "andy" id: 20 price_sat: 2 }
            asks: { account: "andy" id: 10 price_sat: 100 }
            asks:
              {
                account: "domob"
                id: 1
                min_units: 1
                max_units: 2
                price_sat: 123
              }
          }
      }
    assets:
      {
        key: "silver"
        value:
          {
            asset: "silver"
            bids: { account: "andy" id: 30 price_sat: 1 }
          }
      }
  )"));
}

TEST_F (OrderbookTests, UpdatesForAccount)
{
  OrderbookWithoutTimeout o;

  UpdateOrders (o, R"(
    account: "domob"
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 123 }
      }
    orders:
      {
        key: 2
        value: { asset: "silver" type: BID price_sat: 50 }
      }
  )");

  UpdateOrders (o, R"(
    account: "andy"
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 100 }
      }
  )");

  EXPECT_THAT (o.GetByAsset (), EqualsOrdersByAsset (R"(
    assets:
      {
        key: "gold"
        value:
          {
            asset: "gold"
            asks: { account: "andy" id: 1 price_sat: 100 }
            asks: { account: "domob" id: 1 price_sat: 123 }
          }
      }
    assets:
      {
        key: "silver"
        value:
          {
            asset: "silver"
            bids: { account: "domob" id: 2 price_sat: 50 }
          }
      }
  )"));

  UpdateOrders (o, R"(
    account: "domob"
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 42 }
      }
    orders:
      {
        key: 3
        value: { asset: "gold" type: BID price_sat: 1 }
      }
  )");

  EXPECT_THAT (o.GetByAsset (), EqualsOrdersByAsset (R"(
    assets:
      {
        key: "gold"
        value:
          {
            asset: "gold"
            bids: { account: "domob" id: 3 price_sat: 1 }
            asks: { account: "domob" id: 1 price_sat: 42 }
            asks: { account: "andy" id: 1 price_sat: 100 }
          }
      }
  )"));
}

TEST_F (OrderbookTests, Timeout)
{
  constexpr auto TIMEOUT = std::chrono::milliseconds (100);
  OrderBook o(TIMEOUT);

  UpdateOrders (o, R"(
    account: "domob"
    orders:
      {
        key: 1
        value: { asset: "gold" type: ASK price_sat: 100 }
      }
  )");
  UpdateOrders (o, R"(
    account: "andy"
    orders:
      {
        key: 1
        value: { asset: "gold" type: BID price_sat: 10 }
      }
  )");

  EXPECT_THAT (o.GetForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "andy" id: 1 price_sat: 10 }
    asks: { account: "domob" id: 1 price_sat: 100 }
  )"));

  std::this_thread::sleep_for (0.75 * TIMEOUT);
  UpdateOrders (o, R"(
    account: "andy"
    orders:
      {
        key: 1
        value: { asset: "gold" type: BID price_sat: 15 }
      }
  )");
  std::this_thread::sleep_for (0.75 * TIMEOUT);

  EXPECT_THAT (o.GetForAsset ("gold"), EqualsOrdersForAsset (R"(
    asset: "gold"
    bids: { account: "andy" id: 1 price_sat: 15 }
  )"));

  std::this_thread::sleep_for (TIMEOUT);
  EXPECT_THAT (o.GetByAsset (), EqualsOrdersByAsset (""));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
