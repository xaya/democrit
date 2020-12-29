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

#include "mockxaya.hpp"
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
DEFINE_PROTO_MATCHER (EqualsProcessingMessage, ProcessingMessage)

constexpr auto NO_EXPIRY = std::chrono::seconds (1'000);

} // anonymous namespace

/* ************************************************************************** */

/**
 * TradeManager instance used in tests.  It holds a State instance and
 * some fake data / mock RPC connections.
 */
class TestTradeManager : public State, public MyOrders, public TradeManager
{

private:

  /** The mock time to return as "current" for created trades.  */
  int64_t mockTime;

  /** The user's account name.  */
  const std::string account;

  int64_t
  GetCurrentTime () const override
  {
    return mockTime;
  }

public:

  template <typename XayaRpc>
    explicit TestTradeManager (const std::string& a,
                               TestEnvironment<XayaRpc>& env)
    : State(a),
      MyOrders(static_cast<State&> (*this), NO_EXPIRY),
      TradeManager(static_cast<State&> (*this),
                   static_cast<MyOrders&> (*this),
                   env.GetAssetSpec (), env.GetXayaRpc ()),
      mockTime(0), account(a)
  {}

  void
  SetMockTime (const int64_t t)
  {
    mockTime = t;
  }

  /**
   * Constructs a new Trade instance, based on the given data parsed
   * as text-format TradeState.  The internal list of trades will just
   * contain this one trade in the end.
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

    return std::unique_ptr<Trade> (new Trade (*this, account, *ref));
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

  /**
   * Looks up a trade based on the (maker, id) value and returns a Trade
   * instance for it.  Returns null in case there is no matching trade.
   */
  std::unique_ptr<const Trade>
  LookupTrade (const std::string& maker, const uint64_t id)
  {
    std::unique_ptr<const Trade> res;
    ReadState ([&] (const proto::State& s)
      {
        for (const auto& t : s.trades ())
          {
            if (maker != t.order ().account ())
              continue;
            if (id != t.order ().id ())
              continue;

            CHECK (res == nullptr)
                << "Found multiple trades matching " << maker << " " << id;
            res.reset (new Trade (*this, s.account (), t));
          }
      });

    return res;
  }

  /**
   * Adds a new order to the internal "my orders" instance.  Before calling
   * Add, the internal next ID (i.e. the ID that the order will get assigned)
   * is set to the desired value.
   */
  void
  AddOrder (const uint64_t id, const std::string& data)
  {
    AccessState ([id] (proto::State& s)
      {
        s.set_next_free_id (id);
      });
    CHECK (MyOrders::Add (ParseTextProto<proto::Order> (data)));
  }

  /**
   * Calls ProcessMessage with the given data as text proto and expects
   * no reply.
   */
  void
  ProcessWithoutReply (const std::string& data)
  {
    const auto msg = ParseTextProto<proto::ProcessingMessage> (data);

    proto::ProcessingMessage reply;
    ASSERT_FALSE (ProcessMessage (msg, reply));
  }

  /**
   * Calls ProcessMessage with the given data as text proto, expects
   * to get a reply from it, and returns that reply.
   */
  proto::ProcessingMessage
  ProcessWithReply (const std::string& data)
  {
    const auto msg = ParseTextProto<proto::ProcessingMessage> (data);

    proto::ProcessingMessage reply;
    EXPECT_TRUE (ProcessMessage (msg, reply));

    return reply;
  }

  /**
   * Returns the internal proto state data for a Trade instance.  This is
   * just used to expose that private data through this class being a friend
   * of Trade.
   */
  static const proto::TradeState&
  GetInternalState (const Trade& t)
  {
    return t.pb;
  }

  /**
   * Exposes Trade::ConstructTransaction to tests.  It uses an outpoint
   * given by txid and n.
   */
  static std::string
  ConstructTransaction (const Trade& t, const std::string& txid,
                        const unsigned n)
  {
    proto::OutPoint nameIn;
    nameIn.set_hash (txid);
    nameIn.set_n (n);
    return t.ConstructTransaction (*t.checker, nameIn);
  }

  /**
   * Exposes a Trade's checker variable to tests.
   */
  static TradeChecker&
  GetTradeChecker (const Trade& t)
  {
    return *t.checker;
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

  TestEnvironment<MockXayaRpcServer> env;
  TestTradeManager tm;

  TradeStateTests ()
    : tm("me", env)
  {}

  /**
   * Returns the public info data (Trade::GetPublicInfo) for the given
   * TradeState from text proto.
   */
  proto::Trade
  GetPublicInfo (const std::string& data)
  {
    return tm.GetTrade (data)->GetPublicInfo ();
  }

  /**
   * Applies a ProcessingMessage given in text format to a trade of
   * text-format state, and returns the trade's resulting internal state.
   */
  proto::TradeState
  HandleMessage (const std::string& stateBefore, const std::string& msg)
  {
    auto t = tm.GetTrade (stateBefore);
    t->HandleMessage (ParseTextProto<proto::ProcessingMessage> (msg));
    return tm.GetInternalState (*t);
  }

  /**
   * Applies a ProcessingMessage to a trade of given state, and expects
   * that this does not change the state at all.
   */
  void
  ExpectNoStateChange (const std::string& stateBefore, const std::string& msg)
  {
    EXPECT_THAT (HandleMessage (stateBefore, msg),
                 EqualsTradeState (stateBefore));
  }

  /**
   * Checks that the trade with state as per the passed-in text proto
   * has no reply to the counterparty, and also does not modify the state.
   */
  void
  ExpectNoReply (const std::string& state)
  {
    EXPECT_THAT (ExpectNoReplyAndGetNewState (state), EqualsTradeState (state));
  }

  /**
   * Checks that the trade with state as per the passed-in text proto
   * has no reply to the counterparty, and returns the new state.
   */
  proto::TradeState
  ExpectNoReplyAndGetNewState (const std::string& state)
  {
    auto t = tm.GetTrade (state);

    proto::ProcessingMessage reply;
    EXPECT_FALSE (t->HasReply (reply));

    return tm.GetInternalState (*t);
  }

  /**
   * Checks that the trade with given state has a reply to the counterparty,
   * that the new state (after calling HasReply) matches the expected one,
   * and returns the reply message.
   */
  proto::ProcessingMessage
  ExpectReplyAndNewState (const std::string& stateBefore,
                          const std::string& stateAfter)
  {
    auto t = tm.GetTrade (stateBefore);
    proto::ProcessingMessage reply;
    EXPECT_TRUE (t->HasReply (reply));
    EXPECT_THAT (tm.GetInternalState (*t), EqualsTradeState (stateAfter));
    return reply;
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

using TradeSellerDataTests = TradeStateTests;

TEST_F (TradeSellerDataTests, BuyerSentData)
{
  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: ASK }
  )", R"(
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )");
}

TEST_F (TradeSellerDataTests, AlreadyHaveData)
{
  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: BID }
    seller_data: { name_address: "old 1" chi_address: "old 2" }
  )", R"(
    seller_data: { name_address: "new 1" chi_address: "new 2" }
  )");
}

TEST_F (TradeSellerDataTests, InvalidData)
{
  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: BID }
  )", R"(
    seller_data: { chi_address: "addr 2" }
  )");

  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: BID }
  )", R"(
    seller_data: { name_address: "addr 1" }
  )");

  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: BID }
  )", R"(
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: {}
      }
  )");

  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: BID }
  )", R"(
    seller_data:
      {
        name_address: "addr"
        chi_address: "addr"
      }
  )");
}

TEST_F (TradeSellerDataTests, AddsSellerData)
{
  EXPECT_THAT (
      HandleMessage (R"(
        state: INITIATED
        order: { account: "me" type: BID }
      )", R"(
        seller_data: { name_address: "addr 1" chi_address: "addr 2" }
      )"),
      EqualsTradeState (R"(
        state: INITIATED
        order: { account: "me" type: BID }
        seller_data: { name_address: "addr 1" chi_address: "addr 2" }
      )"));
}

TEST_F (TradeSellerDataTests, BuyerDoesNotConstructSellerData)
{
  ExpectNoReply (R"(
    state: INITIATED
    order: { account: "me" type: BID }
  )");
}

TEST_F (TradeSellerDataTests, DoesNotConstructSecondSellerData)
{
  ExpectNoReply (R"(
    state: INITIATED
    order: { account: "me" type: ASK }
    seller_data: {}
  )");
}

TEST_F (TradeSellerDataTests, AddsAndRepliesSellerData)
{
  EXPECT_THAT (
      ExpectReplyAndNewState (R"(
        state: INITIATED
        order: { id: 1 account: "me" type: ASK }
        counterparty: "other"
      )", R"(
        state: INITIATED
        order: { id: 1 account: "me" type: ASK }
        counterparty: "other"
        seller_data:
          {
            name_address: "addr 1"
            chi_address: "addr 2"
            name_output: { hash: "me txid" n: 12 }
          }
      )"),
      EqualsProcessingMessage (R"(
        counterparty: "other"
        identifier: "me\n1"
        seller_data: { name_address: "addr 1" chi_address: "addr 2" }
      )"));
}

/* ************************************************************************** */

using TradeReceivingPsbtTests = TradeStateTests;

TEST_F (TradeReceivingPsbtTests, NewPsbtAdded)
{
  EXPECT_THAT (
      HandleMessage (R"(
        state: INITIATED
        order: { account: "me" type: BID }
        our_psbt: "foo"
      )", R"(
        psbt: { psbt: "bar" }
      )"),
      EqualsTradeState (R"(
        state: INITIATED
        order: { account: "me" type: BID }
        our_psbt: "foo"
        their_psbt: "bar"
      )"));
}

TEST_F (TradeReceivingPsbtTests, AlreadyHavePsbt)
{
  ExpectNoStateChange (R"(
    state: INITIATED
    order: { account: "me" type: ASK }
    their_psbt: "foo"
  )", R"(
    psbt: { psbt: "bar" }
  )");
}

/* ************************************************************************** */

class TradeBuyerTransactionTests : public TradeStateTests
{

protected:

  TradeBuyerTransactionTests ()
  {
    const auto blk = MockXayaRpcServer::GetBlockHash (10);

    auto& srv = env.GetXayaServer ();
    srv.SetBestBlock (blk);
    srv.AddUtxo ("me txid", 12);
    srv.AddUtxo ("other txid", 12);

    auto& spec = env.GetAssetSpec ();
    spec.SetBalance ("me", "gold", 100);
    spec.SetBalance ("other", "gold", 100);
    spec.SetBlock (blk);
  }

  /**
   * Sets up the mock expectations for constructing the transaction
   * to the given buyer for 3 gold and with standard "addr 1" / "addr 2"
   * seller data.  The PSBT identifier of the unsigned tx will be "unsigned".
   */
  void
  PrepareGoldTransaction (const std::string& buyer, const std::string& seller)
  {
    const std::string move =
        R"({"g":{"dem":{},"test":{"amount":3,"asset":"gold","to":")"
          + buyer + R"("}}})";
    const auto sd = ParseTextProto<proto::SellerData> (R"(
      name_address: "addr 1"
      chi_address: "addr 2"
    )");
    env.GetXayaServer ().PrepareConstructTransaction ("unsigned", seller,
                                                      12, sd, 30, move);
  }

};

TEST_F (TradeBuyerTransactionTests, ConstructTransaction)
{
  tm.AddTrade (R"(
    state: INITIATED
    start_time: 1
    order:
      {
        id: 42
        account: "me"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
      }
  )");

  auto t = tm.LookupTrade ("me", 42);
  ASSERT_NE (t, nullptr);
  const auto& pb = tm.GetInternalState (*t);
  const auto& checker = tm.GetTradeChecker (*t);

  env.GetXayaServer ().PrepareConstructTransaction (
      "unsigned", "other",
      13, pb.seller_data (), 30,
      checker.GetNameUpdateValue ());

  /* ConstructTransaction just does its magic, without any particular
     corner-cases that we need to check.  But it should at least work
     and also produce a transaction that the seller deems valid.  */
  ASSERT_EQ (tm.ConstructTransaction (*t, "other txid", 13), "unsigned");
  ASSERT_TRUE (checker.CheckForSellerOutputs ("unsigned", pb.seller_data ()));
}

TEST_F (TradeBuyerTransactionTests, WaitingForSellerData)
{
  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 3
    counterparty: "other"
  )");
}

TEST_F (TradeBuyerTransactionTests, AlreadyHavePsbt)
{
  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 3
    counterparty: "other"
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
    our_psbt: "foo"
  )");
}

TEST_F (TradeBuyerTransactionTests, TradeCheckFails)
{
  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 1001
    counterparty: "other"
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )");
}

TEST_F (TradeBuyerTransactionTests, BuyerSignedEverything)
{
  PrepareGoldTransaction ("me", "other");
  env.GetXayaServer ().SetSignedPsbt ("signed", "unsigned",
                                      {"buyer txid", "other txid"});

  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        id: 42
        account: "me"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 3
    counterparty: "other"
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )");
}

TEST_F (TradeBuyerTransactionTests, BuyerIsMaker)
{
  PrepareGoldTransaction ("me", "other");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"buyer txid"});

  EXPECT_THAT (
      ExpectReplyAndNewState (R"(
            state: INITIATED
            order:
              {
                id: 42
                account: "me"
                asset: "gold"
                price_sat: 10
                type: BID
              }
            units: 3
            counterparty: "other"
            seller_data: { name_address: "addr 1" chi_address: "addr 2" }
          )", R"(
            state: INITIATED
            order:
              {
                id: 42
                account: "me"
                asset: "gold"
                price_sat: 10
                type: BID
              }
            units: 3
            counterparty: "other"
            seller_data: { name_address: "addr 1" chi_address: "addr 2" }
            our_psbt: "partial"
      )"),
      EqualsProcessingMessage (R"(
        counterparty: "other"
        identifier: "me\n42"
        psbt: { psbt: "unsigned" }
      )"));
}

TEST_F (TradeBuyerTransactionTests, BuyerIsTaker)
{
  PrepareGoldTransaction ("me", "other");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"buyer txid"});

  EXPECT_THAT (
      ExpectReplyAndNewState (R"(
            state: INITIATED
            order:
              {
                id: 42
                account: "other"
                asset: "gold"
                price_sat: 10
                type: ASK
              }
            units: 3
            counterparty: "other"
            seller_data: { name_address: "addr 1" chi_address: "addr 2" }
          )", R"(
            state: PENDING
            order:
              {
                id: 42
                account: "other"
                asset: "gold"
                price_sat: 10
                type: ASK
              }
            units: 3
            counterparty: "other"
            seller_data: { name_address: "addr 1" chi_address: "addr 2" }
            our_psbt: "partial"
      )"),
      EqualsProcessingMessage (R"(
        counterparty: "other"
        identifier: "other\n42"
        psbt: { psbt: "partial" }
      )"));
}

/* ************************************************************************** */

using TradeSellerTransactionTests = TradeBuyerTransactionTests;

TEST_F (TradeSellerTransactionTests, NoTransactionYet)
{
  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: ASK
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 12 }
      }
  )");
}

TEST_F (TradeSellerTransactionTests, OutputsCheckFails)
{
  PrepareGoldTransaction ("other", "me");

  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: ASK
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "wrong address"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 12 }
      }
    their_psbt: "unsigned"
  )");
}

TEST_F (TradeSellerTransactionTests, SignedWrongInput)
{
  PrepareGoldTransaction ("other", "me");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"me txid"});

  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: ASK
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 999 }
      }
    their_psbt: "unsigned"
  )");
}

TEST_F (TradeSellerTransactionTests, SellerMakerTxNotComplete)
{
  PrepareGoldTransaction ("other", "me");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"me txid"});

  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "me"
        asset: "gold"
        price_sat: 10
        type: ASK
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 12 }
      }
    their_psbt: "unsigned"
  )");
}

TEST_F (TradeSellerTransactionTests, SellerTakerTxComplete)
{
  PrepareGoldTransaction ("other", "me");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"buyer txid"});
  env.GetXayaServer ().SetSignedPsbt ("signed", "partial", {"me txid"});

  ExpectNoReply (R"(
    state: INITIATED
    order:
      {
        account: "other"
        asset: "gold"
        price_sat: 10
        type: BID
      }
    units: 3
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 12 }
      }
    their_psbt: "partial"
  )");
}

TEST_F (TradeSellerTransactionTests, SellerIsMaker)
{
  PrepareGoldTransaction ("other", "me");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"buyer txid"});
  env.GetXayaServer ().SetSignedPsbt ("full", "partial", {"me txid"});

  EXPECT_THAT (
      ExpectNoReplyAndGetNewState (R"(
        state: INITIATED
        order:
          {
            account: "me"
            asset: "gold"
            price_sat: 10
            type: ASK
          }
        units: 3
        counterparty: "other"
        seller_data:
          {
            name_address: "addr 1"
            chi_address: "addr 2"
            name_output: { hash: "me txid" n: 12 }
          }
        their_psbt: "partial"
      )"),
    EqualsTradeState (R"(
        state: PENDING
        order:
          {
            account: "me"
            asset: "gold"
            price_sat: 10
            type: ASK
          }
        units: 3
        counterparty: "other"
        seller_data:
          {
            name_address: "addr 1"
            chi_address: "addr 2"
            name_output: { hash: "me txid" n: 12 }
          }
        their_psbt: "partial"
        our_psbt: "full"
    )"));
}

TEST_F (TradeSellerTransactionTests, SellerIsTaker)
{
  PrepareGoldTransaction ("other", "me");
  env.GetXayaServer ().SetSignedPsbt ("partial", "unsigned", {"me txid"});

  EXPECT_THAT (
      ExpectReplyAndNewState (R"(
        state: INITIATED
        order:
          {
            id: 42
            account: "other"
            asset: "gold"
            price_sat: 10
            type: BID
          }
        units: 3
        counterparty: "other"
        seller_data:
          {
            name_address: "addr 1"
            chi_address: "addr 2"
            name_output: { hash: "me txid" n: 12 }
          }
        their_psbt: "unsigned"
      )", R"(
        state: PENDING
        order:
          {
            id: 42
            account: "other"
            asset: "gold"
            price_sat: 10
            type: BID
          }
        units: 3
        counterparty: "other"
        seller_data:
          {
            name_address: "addr 1"
            chi_address: "addr 2"
            name_output: { hash: "me txid" n: 12 }
          }
        their_psbt: "unsigned"
        our_psbt: "partial"
      )"),
      EqualsProcessingMessage (R"(
        counterparty: "other"
        identifier: "other\n42"
        psbt: { psbt: "partial" }
      )"));
}

/* ************************************************************************** */

using TradeMakerBuyerFinalisationTests = TradeBuyerTransactionTests;

TEST_F (TradeMakerBuyerFinalisationTests, Success)
{
  PrepareGoldTransaction ("me", "other");
  env.GetXayaServer ().SetSignedPsbt ("my partial", "unsigned", {"buyer txid"});
  env.GetXayaServer ().SetSignedPsbt ("other partial", "unsigned",
                                      {"other txid"});

  EXPECT_THAT (
      ExpectNoReplyAndGetNewState (R"(
        state: INITIATED
        order:
          {
            account: "me"
            asset: "gold"
            price_sat: 10
            type: BID
          }
        units: 3
        counterparty: "other"
        seller_data: { name_address: "addr 1" chi_address: "addr 2" }
        our_psbt: "my partial"
        their_psbt: "their partial"
      )"),
    EqualsTradeState (R"(
        state: PENDING
        order:
          {
            account: "me"
            asset: "gold"
            price_sat: 10
            type: BID
          }
        units: 3
        counterparty: "other"
        seller_data: { name_address: "addr 1" chi_address: "addr 2" }
        our_psbt: "my partial"
        their_psbt: "their partial"
    )"));
}

/* FIXME: Test for the situation where combinepsbt returns a non-completed
   transaction (because their_psbt was unsigned).  */

/* ************************************************************************** */

class TradeManagerTests : public testing::Test
{

protected:

  TestEnvironment<MockXayaRpcServer> env;
  TestTradeManager tm;

  TradeManagerTests ()
    : tm("me", env)
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

TEST_F (TradeManagerTests, TakingSellOrder)
{
  proto::ProcessingMessage msg;
  ASSERT_TRUE (tm.TakeOrder (ParseTextProto<proto::Order> (R"(
    account: "other"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 64
    type: ASK
  )"), 100, msg));
  EXPECT_THAT (msg, EqualsProcessingMessage (R"(
    counterparty: "other"
    identifier: "other\n42"
    taking_order: { id: 42 units: 100 }
  )"));
}

TEST_F (TradeManagerTests, TakingBuyOrder)
{
  proto::ProcessingMessage msg;
  ASSERT_TRUE (tm.TakeOrder (ParseTextProto<proto::Order> (R"(
    account: "other"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 64
    type: BID
  )"), 100, msg));
  EXPECT_THAT (msg, EqualsProcessingMessage (R"(
    counterparty: "other"
    identifier: "other\n42"
    taking_order: { id: 42 units: 100 }
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )"));
}

TEST_F (TradeManagerTests, ProcessingTakeOrderUnavailable)
{
  tm.AddOrder (42, R"(
    asset: "gold"
    max_units: 10
    price_sat: 5
    type: BID
  )");

  proto::Order o;
  ASSERT_TRUE (tm.TryLock (42, o));

  tm.ProcessWithoutReply (R"(
    counterparty: "other"
    identifier: "me\n10"
    taking_order: { id: 10 units: 1 }
  )");

  tm.ProcessWithoutReply (R"(
    counterparty: "other"
    identifier: "me\n42"
    taking_order: { id: 42 units: 1 }
  )");

  tm.Unlock (42);
}

TEST_F (TradeManagerTests, ProcessingTakeOrderWithWrongUnits)
{
  tm.AddOrder (42, R"(
    asset: "gold"
    max_units: 10
    price_sat: 5
    type: BID
  )");

  tm.ProcessWithoutReply (R"(
    counterparty: "other"
    identifier: "me\n42"
    taking_order: { id: 42 units: 11 }
  )");

  /* The order should not be locked now!  */
  proto::Order o;
  ASSERT_TRUE (tm.TryLock (42, o));
}

TEST_F (TradeManagerTests, ProcessingTakeSellOrder)
{
  tm.AddOrder (42, R"(
    asset: "gold"
    max_units: 10
    price_sat: 5
    type: ASK
  )");

  EXPECT_THAT (tm.ProcessWithReply (R"(
    counterparty: "other"
    identifier: "me\n42"
    taking_order: { id: 42 units: 10 }
  )"), EqualsProcessingMessage (R"(
    counterparty: "other"
    identifier: "me\n42"
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )"));

  /* The order should be locked now.  */
  EXPECT_THAT (tm.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "me"
    orders:
      {
        key: 42
        value:
          {
            asset: "gold"
            max_units: 10
            price_sat: 5
            type: ASK
            locked: true
          }
      }
  )"));

  auto t = tm.LookupTrade ("me", 42);
  ASSERT_NE (t, nullptr);
  EXPECT_THAT (tm.GetInternalState (*t), EqualsTradeState (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "me"
        id: 42
        asset: "gold"
        max_units: 10
        type: ASK
        price_sat: 5
      }
    units: 10
    counterparty: "other"
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "me txid" n: 12 }
      }
  )"));
}

TEST_F (TradeManagerTests, ProcessingTakeBuyOrder)
{
  tm.AddOrder (42, R"(
    asset: "gold"
    max_units: 10
    price_sat: 5
    type: BID
  )");

  /* This will kick off transaction creation by the buyer; but in this
     special situation, the trade is actually invalid and thus there
     won't be a reply.  In this test we only want to make sure the trade
     is created correctly, so this doesn't matter (and is the simplest way
     to do this test).  */
  tm.ProcessWithoutReply (R"(
    counterparty: "other"
    identifier: "me\n42"
    taking_order: { id: 42 units: 10 }
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )");

  /* The order should be locked now.  */
  EXPECT_THAT (tm.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "me"
    orders:
      {
        key: 42
        value:
          {
            asset: "gold"
            max_units: 10
            price_sat: 5
            type: BID
            locked: true
          }
      }
  )"));

  auto t = tm.LookupTrade ("me", 42);
  ASSERT_NE (t, nullptr);
  EXPECT_THAT (tm.GetInternalState (*t), EqualsTradeState (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "me"
        id: 42
        asset: "gold"
        max_units: 10
        type: BID
        price_sat: 5
      }
    units: 10
    counterparty: "other"
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
  )"));
}

TEST_F (TradeManagerTests, TakeOrderException)
{
  /* p/invalid is considered a non-existing name by the mock Xaya RPC
     server.  Thus taking a buy order with this name (which is then the
     seller) will throw when it tries to fill in the seller data.  This should
     be handled gracefully.  */

  TestTradeManager inv("invalid", env);

  proto::ProcessingMessage msg;
  ASSERT_FALSE (inv.TakeOrder (ParseTextProto<proto::Order> (R"(
    account: "other"
    id: 42
    asset: "gold"
    max_units: 100
    price_sat: 64
    type: BID
  )"), 100, msg));

  /* No new element should have been added to our internal list of
     trades either.  */
  EXPECT_THAT (inv.GetTrades (), ElementsAre ());
}

TEST_F (TradeManagerTests, ProcessMessageException)
{
  TestTradeManager inv("invalid", env);
  inv.SetMockTime (123);

  inv.AddOrder (42, R"(
    asset: "gold"
    max_units: 10
    price_sat: 5
    type: ASK
  )");

  inv.ProcessWithoutReply (R"(
    counterparty: "other"
    identifier: "invalid\n42"
    taking_order: { id: 42 units: 10 }
  )");

  /* The trade should have been added, but without seller data (since the
     updating step raised the exception).  */
  auto t = inv.LookupTrade ("invalid", 42);
  ASSERT_NE (t, nullptr);
  EXPECT_THAT (inv.GetInternalState (*t), EqualsTradeState (R"(
    state: INITIATED
    start_time: 123
    order:
      {
        account: "invalid"
        id: 42
        asset: "gold"
        max_units: 10
        type: ASK
        price_sat: 5
      }
    units: 10
    counterparty: "other"
  )"));
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

/**
 * These tests use *two* TradeManager's that actually exchange messages
 * with each other to simulate the entire trade flow.
 */
class TradeFlowTests : public testing::Test
{

protected:

  /* We need two separate mock environments for buyer and seller so that
     they can react differently to "walletprocesspsbt(unsigned)".  */
  TestEnvironment<MockXayaRpcServer> envBuyer;
  TestEnvironment<MockXayaRpcServer> envSeller;

  TestTradeManager buyer;
  TestTradeManager seller;

  TradeFlowTests ()
    : buyer("buyer", envBuyer), seller("seller", envSeller)
  {
    buyer.SetMockTime (123);
    seller.SetMockTime (123);

    const auto blk = MockXayaRpcServer::GetBlockHash (10);

    for (auto* env : {&envBuyer, &envSeller})
      {
        auto& srv = env->GetXayaServer ();
        srv.SetBestBlock (blk);
        srv.AddUtxo ("seller txid", 12);

        auto& spec = env->GetAssetSpec ();
        spec.InitialiseAccount ("buyer");
        spec.SetBalance ("seller", "gold", 10);
        spec.SetBalance ("seller", "silver", 10);
        spec.SetBlock (blk);
      }
  }

  /**
   * Sets up the mock expectations for constructing the trade transaction
   * for "buyer" and "seller" and the given amount of asset.
   */
  void
  PrepareTradeTransaction (const Asset& asset, const Amount units,
                           const Amount total)
  {
    std::ostringstream mv;
    mv << R"({"g":{"dem":{},"test":{"amount":)" << units
       << R"(,"asset":")" << asset << R"(","to":"buyer"}}})";
    const auto sd = ParseTextProto<proto::SellerData> (R"(
      name_address: "addr 1"
      chi_address: "addr 2"
    )");

    /* Both buyer and seller need to know about the transaction.  */
    for (auto* env : {&envBuyer, &envSeller})
      env->GetXayaServer ().PrepareConstructTransaction (
          "unsigned", "seller", 12, sd, total, mv.str ());
  }

  /**
   * Runs a "TakeOrder" call on the given trade manager, and then starts
   * passing back and forth the reply messages between the two until one
   * of them no longer has a reply.
   */
  void
  TakeOrder (TradeManager& tm, const Amount units, const std::string& order)
  {
    CHECK (&tm == &buyer || &tm == &seller);

    proto::ProcessingMessage msg;
    if (!tm.TakeOrder (ParseTextProto<proto::Order> (order), units, msg))
      return;

    while (true)
      {
        TradeManager* msgFor;
        if (msg.counterparty () == "buyer")
          {
            msgFor = &buyer;
            msg.set_counterparty ("seller");
          }
        else if (msg.counterparty () == "seller")
          {
            msgFor = &seller;
            msg.set_counterparty ("buyer");
          }
        else
          LOG (FATAL) << "Unexpected counterparty:\n" << msg.DebugString ();

        proto::ProcessingMessage reply;
        if (!msgFor->ProcessMessage (msg, reply))
          return;

        msg = reply;
      }
  }

};

TEST_F (TradeFlowTests, TakingBuyOrder)
{
  PrepareTradeTransaction ("gold", 3, 30);

  /* Setting up the signing here is a bit tricky.  We want both seller and
     buyer to know the PSBTs involved, but we want the seller and buyer
     to return their respective signed PSBTs from walletprocesspsbt.  */
  for (auto* env : {&envBuyer, &envSeller})
    {
      auto& srv = env->GetXayaServer ();
      srv.SetSignedPsbt ("buyer signed", "unsigned", {"buyer txid"});
      srv.SetSignedPsbt ("seller signed", "unsigned", {"seller txid"});
    }
  envBuyer.GetXayaServer ()
      .SetSignedPsbt ("buyer signed", "unsigned", {"buyer txid"});
  envSeller.GetXayaServer ()
      .SetSignedPsbt ("seller signed", "unsigned", {"seller txid"});

  buyer.AddOrder (1, R"(
    asset: "gold"
    max_units: 5
    price_sat: 10
    type: BID
  )");

  TakeOrder (seller, 3, R"(
    account: "buyer"
    id: 1
    asset: "gold"
    max_units: 5
    price_sat: 10
    type: BID
  )");

  EXPECT_THAT (buyer.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "buyer"
    orders:
      {
        key: 1
        value:
          {
            asset: "gold"
            max_units: 5
            price_sat: 10
            type: BID
            locked: true
          }
      }
  )"));
  EXPECT_THAT (buyer.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: PENDING
      start_time: 123
      counterparty: "seller"
      type: BID
      asset: "gold"
      units: 3
      price_sat: 10
      role: MAKER
    )")
  ));
  EXPECT_THAT (buyer.GetInternalState (*buyer.LookupTrade ("buyer", 1)),
               EqualsTradeState (R"(
    state: PENDING
    start_time: 123
    order:
      {
        account: "buyer"
        id: 1
        asset: "gold"
        max_units: 5
        type: BID
        price_sat: 10
      }
    counterparty: "seller"
    units: 3
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
    our_psbt: "buyer signed"
    their_psbt: "seller signed"
  )"));

  EXPECT_THAT (seller.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "seller"
  )"));
  EXPECT_THAT (seller.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: PENDING
      start_time: 123
      counterparty: "buyer"
      type: ASK
      asset: "gold"
      units: 3
      price_sat: 10
      role: TAKER
    )")
  ));
  EXPECT_THAT (seller.GetInternalState (*seller.LookupTrade ("buyer", 1)),
               EqualsTradeState (R"(
    state: PENDING
    start_time: 123
    order:
      {
        account: "buyer"
        id: 1
        asset: "gold"
        max_units: 5
        type: BID
        price_sat: 10
      }
    counterparty: "buyer"
    units: 3
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "seller txid" n: 12 }
      }
    their_psbt: "unsigned"
    our_psbt: "seller signed"
  )"));
}

TEST_F (TradeFlowTests, TakingSellOrder)
{
  PrepareTradeTransaction ("silver", 1, 5);
  for (auto* env : {&envBuyer, &envSeller})
    {
      auto& srv = env->GetXayaServer ();
      srv.SetSignedPsbt ("partial", "unsigned", {"buyer txid"});
      srv.SetSignedPsbt ("signed", "partial", {"seller txid"});
    }

  seller.AddOrder (1, R"(
    asset: "silver"
    max_units: 1
    price_sat: 5
    type: ASK
  )");

  TakeOrder (buyer, 1, R"(
    account: "seller"
    id: 1
    asset: "silver"
    max_units: 1
    price_sat: 5
    type: ASK
  )");

  EXPECT_THAT (buyer.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "buyer"
  )"));
  EXPECT_THAT (buyer.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: PENDING
      start_time: 123
      counterparty: "seller"
      type: BID
      asset: "silver"
      units: 1
      price_sat: 5
      role: TAKER
    )")
  ));
  EXPECT_THAT (buyer.GetInternalState (*buyer.LookupTrade ("seller", 1)),
               EqualsTradeState (R"(
    state: PENDING
    start_time: 123
    order:
      {
        account: "seller"
        id: 1
        asset: "silver"
        max_units: 1
        type: ASK
        price_sat: 5
      }
    counterparty: "seller"
    units: 1
    seller_data: { name_address: "addr 1" chi_address: "addr 2" }
    our_psbt: "partial"
  )"));

  EXPECT_THAT (seller.GetOrders (), EqualsOrdersOfAccount (R"(
    account: "seller"
    orders:
      {
        key: 1
        value:
          {
            asset: "silver"
            max_units: 1
            price_sat: 5
            type: ASK
            locked: true
          }
      }
  )"));
  EXPECT_THAT (seller.GetTrades (), ElementsAre (
    EqualsTrade (R"(
      state: PENDING
      start_time: 123
      counterparty: "buyer"
      type: ASK
      asset: "silver"
      units: 1
      price_sat: 5
      role: MAKER
    )")
  ));
  EXPECT_THAT (seller.GetInternalState (*seller.LookupTrade ("seller", 1)),
               EqualsTradeState (R"(
    state: PENDING
    start_time: 123
    order:
      {
        account: "seller"
        id: 1
        asset: "silver"
        max_units: 1
        type: ASK
        price_sat: 5
      }
    counterparty: "buyer"
    units: 1
    seller_data:
      {
        name_address: "addr 1"
        chi_address: "addr 2"
        name_output: { hash: "seller txid" n: 12 }
      }
    their_psbt: "partial"
    our_psbt: "signed"
  )"));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
