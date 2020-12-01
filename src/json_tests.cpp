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

#include "json.hpp"

#include "proto/orders.pb.h"
#include "testutils.hpp"

#include <gtest/gtest.h>

#include <google/protobuf/util/message_differencer.h>

#include <sstream>

namespace democrit
{
namespace
{

using google::protobuf::util::MessageDifferencer;

/**
 * Parses a string to JSON.
 */
Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  return res;
}

class JsonTests : public testing::Test
{

protected:

  /**
   * Expects that the given text proto converted to JSON equals
   * the given JSON string.
   */
  template <typename Proto>
    static void
    ExpectProtoToJson (const std::string& pb, const std::string& expectedJson)
  {
    const auto obj = ParseTextProto<Proto> (pb);
    ASSERT_EQ (ProtoToJson (obj), ParseJson (expectedJson));
  }

  /**
   * Expects that a given JSON value (parsed from string) can be parsed
   * as proto of the template type, and equals to the expected value (given
   * as text proto).
   */
  template <typename Proto>
    static void
    ExpectProtoFromJson (const std::string& str, const std::string& expectedPb)
  {
    Proto actual;
    ASSERT_TRUE (ProtoFromJson (ParseJson (str), actual));

    const auto expected = ParseTextProto<Proto> (expectedPb);
    EXPECT_TRUE (MessageDifferencer::Equals (actual, expected))
        << "Actual: " << actual.DebugString ()
        << "\nExpected: " << expected.DebugString ();
  }

};

TEST_F (JsonTests, OrderToJson)
{
  ExpectProtoToJson<proto::Order> (R"(
    max_units: 42
    price_sat: 5
  )", R"({
    "min_units": 1,
    "max_units": 42,
    "price_sat": 5
  })");

  ExpectProtoToJson<proto::Order> (R"(
    account: "foo"
    id: 100
    asset: "gold"
    min_units: 3
    max_units: 42
    price_sat: 5
    type: BID
  )", R"({
    "account": "foo",
    "id": 100,
    "asset": "gold",
    "min_units": 3,
    "max_units": 42,
    "price_sat": 5,
    "type": "bid"
  })");

  ExpectProtoToJson<proto::Order> (R"(
    max_units: 1
    price_sat: 2
    type: ASK
    locked: false
  )", R"({
    "min_units": 1,
    "max_units": 1,
    "price_sat": 2,
    "type": "ask"
  })");

  ExpectProtoToJson<proto::Order> (R"(
    max_units: 1
    price_sat: 2
    type: ASK
    locked: true
  )", R"({
    "min_units": 1,
    "max_units": 1,
    "price_sat": 2,
    "type": "ask",
    "locked": true
  })");
}

TEST_F (JsonTests, OrdersOfAccountToJson)
{
  ExpectProtoToJson<proto::OrdersOfAccount> (R"(
    account: "domob"
  )", R"({
    "account": "domob",
    "orders": []
  })");

  ExpectProtoToJson<proto::OrdersOfAccount> (R"(
    account: "domob"
    orders:
      {
        key: 10
        value:
          {
            account: "wrong"
            id: 12345
            asset: "gold"
            max_units: 5
            price_sat: 2
            type: BID
          }
      }
    orders:
      {
        key: 12
        value:
          {
            asset: "gold"
            max_units: 1
            price_sat: 10
            type: ASK
          }
      }
  )", R"({
    "account": "domob",
    "orders":
      [
        {
          "id": 10,
          "asset": "gold",
          "min_units": 1,
          "max_units": 5,
          "price_sat": 2,
          "type": "bid"
        },
        {
          "id": 12,
          "asset": "gold",
          "min_units": 1,
          "max_units": 1,
          "price_sat": 10,
          "type": "ask"
        }
      ]
  })");
}

TEST_F (JsonTests, OrderbookForAssetToJson)
{
  ExpectProtoToJson<proto::OrderbookForAsset> (R"(
    asset: "gold"
    bids:
      {
        account: "domob"
        id: 10
        asset: "silver"
        type: ASK
        max_units: 1
        price_sat: 2
      }
    bids:
      {
        account: "domob"
        id: 20
        max_units: 1
        price_sat: 1
      }
    asks:
      {
        account: "andy"
        id: 10
        max_units: 2
        price_sat: 10
      }
  )", R"({
    "asset": "gold",
    "bids":
      [
        {
          "account": "domob",
          "id": 10,
          "min_units": 1,
          "max_units": 1,
          "price_sat": 2
        },
        {
          "account": "domob",
          "id": 20,
          "min_units": 1,
          "max_units": 1,
          "price_sat": 1
        }
      ],
    "asks":
      [
        {
          "account": "andy",
          "id": 10,
          "min_units": 1,
          "max_units": 2,
          "price_sat": 10
        }
      ]
  })");
}

TEST_F (JsonTests, OrderbookByAssetToJson)
{
  ExpectProtoToJson<proto::OrderbookByAsset> (R"(
    assets:
      {
        key: "gold"
        value:
          {
            asset: "wrong"
            bids: { account: "domob" id: 1 max_units: 1 price_sat: 10 }
          }
      }
    assets:
      {
        key: "silver"
        value:
          {
            asks: { account: "domob" id: 2 max_units: 1 price_sat: 1 }
          }
      }
  )", R"({
    "gold":
      {
        "asset": "gold",
        "bids":
          [
            {
              "account": "domob",
              "id": 1,
              "min_units": 1,
              "max_units": 1,
              "price_sat": 10
            }
          ],
        "asks": []
      },
    "silver":
      {
        "asset": "silver",
        "bids": [],
        "asks":
          [
            {
              "account": "domob",
              "id": 2,
              "min_units": 1,
              "max_units": 1,
              "price_sat": 1
            }
          ]
      }
  })");
}

TEST_F (JsonTests, InvalidOrderFromJson)
{
  const auto invalidOrders = ParseJson (R"([
    42,
    [1, 2, 3],
    "order",
    null,
    {},
    {"max_units": 1},
    {"price_sat": 1},
    {"max_units": -1, "price_sat": 1},
    {"max_units": 1, "price_sat": -1},
    {"max_units": 1, "price_sat": 1, "account": false},
    {"max_units": 1, "price_sat": 1, "id": -5},
    {"max_units": 1, "price_sat": 1, "asset": 10},
    {"max_units": 1, "price_sat": 1, "min_units": -5},
    {"max_units": 1, "price_sat": 1, "type": null},
    {"max_units": 1, "price_sat": 1, "type": "invalid"}
  ])");

  for (const auto& o : invalidOrders)
    {
      proto::Order dummy;
      ASSERT_FALSE (ProtoFromJson (o, dummy));
    }
}

TEST_F (JsonTests, ValidOrderFromJson)
{
  ExpectProtoFromJson<proto::Order> (R"({
    "price_sat": 10,
    "max_units": 4
  })", R"(
    price_sat: 10
    max_units: 4
  )");

  ExpectProtoFromJson<proto::Order> (R"({
    "account": "domob",
    "id": 123,
    "asset": "gold",
    "min_units": 3,
    "max_units": 3,
    "price_sat": 1,
    "type": "bid"
  })", R"(
    account: "domob"
    id: 123
    asset: "gold"
    min_units: 3
    max_units: 3
    price_sat: 1
    type: BID
  )");

  ExpectProtoFromJson<proto::Order> (R"({
    "price_sat": 100,
    "max_units": 1,
    "type": "ask"
  })", R"(
    price_sat: 100
    max_units: 1
    type: ASK
  )");
}

} // anonymous namespace
} // namespace democrit
