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

#include "private/mucclient.hpp"

#include "testutils.hpp"

#include <gloox/stanzaextension.h>
#include <gloox/tag.h>

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <queue>

using testing::_;
using testing::AnyNumber;

namespace democrit
{

/* ************************************************************************** */

class MucClientTests : public testing::Test
{

protected:

  /**
   * Gives direct access to the MUCRoom instance insice a MucClient (which
   * is normally private).
   */
  static gloox::MUCRoom&
  AccessRoom (MucClient& c)
  {
    CHECK (c.room != nullptr);
    return *c.room;
  }

  /**
   * Expects that the given nickname has no known full JID for the client.
   */
  static void
  ExpectUnknownNick (const MucClient& c, const std::string& nick)
  {
    gloox::JID jid;
    ASSERT_FALSE (c.ResolveNickname (nick, jid));
  }

  /**
   * Expects that the given nickname has a known full JID and that it matches
   * the given expected one.
   */
  static void
  ExpectNickJid (const MucClient& c, const std::string& nick,
                 const gloox::JID& expected)
  {
    gloox::JID jid;
    ASSERT_TRUE (c.ResolveNickname (nick, jid));
    ASSERT_EQ (jid.full (), expected.full ());
  }

};

namespace
{

/** XML namespace for our test stanza extension.  */
#define XMLNS " https://xaya.io/democrit/test/"

/**
 * Basic stanza extension that we use in tests of messaging.
 */
class TestExtension : public gloox::StanzaExtension
{

private:

  /**
   * The extension just holds a string, which we use to compare it to
   * expectations in tests.
   */
  std::string value;

public:

  /** The extension type.  We only use one in the tests here anyway.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 1;

  /**
   * Constructs an instance with the given value.  Without a value, it
   * can be used e.g. as factory.
   */
  explicit TestExtension (const std::string& v = "")
    : StanzaExtension(EXT_TYPE), value(v)
  {}

  /**
   * Constructs it from a tag.
   */
  explicit TestExtension (const gloox::Tag& t)
    : StanzaExtension(EXT_TYPE)
  {
    value = t.cdata ();
  }

  const std::string&
  GetValue () const
  {
    return value;
  }

  const std::string&
  filterString () const override
  {
    static const std::string filter = "/*/test[@xmlns='" XMLNS "']";
    return filter;
  }

  gloox::StanzaExtension*
  newInstance (const gloox::Tag* tag) const override
  {
    return new TestExtension (*tag);
  }

  gloox::StanzaExtension*
  clone () const override
  {
    return new TestExtension (value);
  }

  gloox::Tag*
  tag () const override
  {
    auto res = std::make_unique<gloox::Tag> ("test", value);
    CHECK (res->setXmlns (XMLNS));
    return res.release ();
  }

};

/**
 * Test client that registers our stanza extension and has handlers for
 * messages and other things we may want to test.
 */
class TestClient : public MucClient
{

private:

  /**
   * Expected data for received messages (private or broadcast).
   */
  struct ExpectedMessage
  {
    gloox::JID jid;
    std::string value;
    bool priv;
  };

  /**
   * Queue of received and not yet processed (matched against expectations)
   * messages.
   */
  std::queue<ExpectedMessage> received;

  /** Condition variable that is notified when a new message is received.  */
  std::condition_variable cv;

  /** Lock for messages and the cv.  */
  std::mutex mut;

  /**
   * Add a received message to the queue.
   */
  void
  AddMessage (const ExpectedMessage& msg)
  {
    std::lock_guard<std::mutex> lock(mut);
    received.push (msg);
    cv.notify_all ();
  }

  /**
   * Handles a message (private or public) that was received.  This looks
   * for our test extension, and if present, calls AddMessage.
   */
  void
  HandleMessage (const gloox::JID& sender, const gloox::Stanza& msg,
                 const bool priv)
  {
    auto* ext = msg.findExtension<TestExtension> (TestExtension::EXT_TYPE);
    if (ext == nullptr)
      {
        LOG (WARNING)
            << "Ignoring message from " << sender.full ()
            << " that does not have the test extension";
        return;
      }
    AddMessage ({sender, ext->GetValue (), priv});
  }

protected:

  void
  HandleMessage (const gloox::JID& sender, const gloox::Stanza& msg) override
  {
    HandleMessage (sender, msg, false);
  }

  void
  HandlePrivate (const gloox::JID& sender, const gloox::Stanza& msg) override
  {
    HandleMessage (sender, msg, true);
  }

public:

  explicit TestClient (const gloox::JID& id, const std::string& pwd,
                       const gloox::JID& rm)
    : MucClient(id, pwd, rm)
  {
    SetRootCA (GetTestCA ());
    RegisterExtension (std::make_unique<TestExtension> ());

    /* By default, we do not care about mock calls to the disconnect
       handler.  Only some tests want those calls explicit.  */
    EXPECT_CALL (*this, HandleDisconnect (_)).Times (AnyNumber ());
  }

  /**
   * At the end of the test, no more messages should have been received.
   */
  ~TestClient ()
  {
    std::lock_guard<std::mutex> lock(mut);
    EXPECT_TRUE (received.empty ()) << "Unexpected messages received";
  }

  /**
   * Publishes a message with test extension.
   */
  void
  Publish (const std::string& value)
  {
    ExtensionData ext;
    ext.push_back (std::make_unique<TestExtension> (value));
    PublishMessage (std::move (ext));
  }

  /**
   * Sends a private message with test extension.
   */
  void
  SendPrivate (const gloox::JID& to, const std::string& value)
  {
    ExtensionData ext;
    ext.push_back (std::make_unique<TestExtension> (value));
    SendMessage (to, std::move (ext));
  }

  /**
   * Expects that the given list of messages are received (waiting for them
   * if not yet).
   */
  void
  ExpectMessages (const std::vector<ExpectedMessage>& expected)
  {
    for (const auto& msg : expected)
      {
        std::unique_lock<std::mutex> lock(mut);
        while (received.empty ())
          cv.wait (lock);

        EXPECT_EQ (received.front ().jid, msg.jid);
        EXPECT_EQ (received.front ().value, msg.value);
        EXPECT_EQ (received.front ().priv, msg.priv);
        received.pop ();
      }
  }

  MOCK_METHOD (void, HandleDisconnect, (const gloox::JID& disconnected),
               (override));

};

/* ************************************************************************** */

using MucConnectionTests = MucClientTests;

TEST_F (MucConnectionTests, Works)
{
  TestClient client(GetTestJid (0), GetPassword (0), GetRoom ("foo"));
  EXPECT_TRUE (client.Connect ());
}

TEST_F (MucConnectionTests, Reconnecting)
{
  TestClient client(GetTestJid (0), GetPassword (0), GetRoom ("foo"));

  ASSERT_TRUE (client.Connect ());
  EXPECT_TRUE (client.IsConnected ());

  client.Disconnect ();
  EXPECT_FALSE (client.IsConnected ());

  ASSERT_TRUE (client.Connect ());
  EXPECT_TRUE (client.IsConnected ());
}

TEST_F (MucConnectionTests, InvalidConnection)
{
  TestClient client(GetTestJid (0), "wrong password", GetRoom ("foo"));
  EXPECT_FALSE (client.Connect ());
}

TEST_F (MucConnectionTests, InvalidRoom)
{
  TestClient client(GetTestJid (0), GetPassword (0), GetRoom ("invalid room"));
  EXPECT_FALSE (client.Connect ());
}

TEST_F (MucConnectionTests, MultipleParticipants)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient client1(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (client1.Connect ());

  TestClient client2(GetTestJid (1), GetPassword (1), room);
  ASSERT_TRUE (client2.Connect ());

  TestClient client3(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (client3.Connect ());
}

TEST_F (MucConnectionTests, KickedFromRoom)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient first(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (first.Connect ());

  TestClient second(GetTestJid (1), GetPassword (1), room);
  ASSERT_TRUE (second.Connect ());

  SleepSome ();
  ASSERT_TRUE (first.IsConnected ());
  ASSERT_TRUE (second.IsConnected ());

  AccessRoom (first).kick (AccessRoom (second).nick ());
  SleepSome ();
  ASSERT_TRUE (first.IsConnected ());
  ASSERT_FALSE (second.IsConnected ());
}

/* ************************************************************************** */

using MucClientNickMapTests = MucClientTests;

TEST_F (MucClientNickMapTests, Works)
{
  const gloox::JID room = GetRoom ("foo");

  const auto firstJid = GetTestJid (0, "first");
  TestClient first(firstJid, GetPassword (0), room);
  ASSERT_TRUE (first.Connect ());

  const auto secondJid = GetTestJid (1, "second");
  TestClient second(secondJid, GetPassword (1), room);
  ASSERT_TRUE (second.Connect ());

  ExpectNickJid (first, AccessRoom (second).nick (), secondJid);
  ExpectNickJid (second, AccessRoom (first).nick (), firstJid);
}

TEST_F (MucClientNickMapTests, UnknownNick)
{
  TestClient client(GetTestJid (0), GetPassword (0), GetRoom ("foo"));
  ASSERT_TRUE (client.Connect ());

  ExpectUnknownNick (client, "invalid");
  ExpectUnknownNick (client, AccessRoom (client).nick ());
}

TEST_F (MucClientNickMapTests, OtherRoom)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient first(GetTestJid (0), GetPassword (0), GetRoom ("foo"));
  ASSERT_TRUE (first.Connect ());

  TestClient second(GetTestJid (1), GetPassword (1), GetRoom ("bar"));
  ASSERT_TRUE (second.Connect ());

  ExpectUnknownNick (first, AccessRoom (second).nick ());
  ExpectUnknownNick (second, AccessRoom (first).nick ());
}

TEST_F (MucClientNickMapTests, SelfDisconnect)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient first(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (first.Connect ());

  TestClient second(GetTestJid (1), GetPassword (1), room);
  ASSERT_TRUE (second.Connect ());
  const std::string secondNick = AccessRoom (second).nick ();

  first.Disconnect ();
  second.Disconnect ();
  ASSERT_TRUE (first.Connect ());

  ExpectUnknownNick (first, secondNick);
}

TEST_F (MucClientNickMapTests, PeerDisconnect)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient first(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (first.Connect ());

  TestClient second(GetTestJid (1), GetPassword (1), room);
  ASSERT_TRUE (second.Connect ());
  const std::string secondNick = AccessRoom (second).nick ();
  second.Disconnect ();

  ExpectUnknownNick (first, secondNick);
}

TEST_F (MucClientNickMapTests, NickChange)
{
  const gloox::JID room = GetRoom ("foo");

  TestClient first(GetTestJid (0), GetPassword (0), room);
  ASSERT_TRUE (first.Connect ());

  const auto secondJid = GetTestJid (1, "second");
  TestClient second(secondJid, GetPassword (1), room);
  ASSERT_TRUE (second.Connect ());
  const std::string secondNick = AccessRoom (second).nick ();

  ExpectNickJid (first, secondNick, secondJid);

  LOG (INFO) << "Changing nick in the room...";
  AccessRoom (second).setNick ("my new nick");
  SleepSome ();

  ExpectUnknownNick (first, secondNick);
  ExpectNickJid (first, "my new nick", secondJid);
}

/* ************************************************************************** */

using MucDisconnectNotificationTests = MucClientTests;

TEST_F (MucDisconnectNotificationTests, Works)
{
  const gloox::JID room = GetRoom ("foo");

  const auto fooJid = GetTestJid (0, "foo");
  TestClient foo(fooJid, GetPassword (0), room);

  const auto barJid = GetTestJid (1, "bar");
  TestClient bar(barJid, GetPassword (1), room);

  EXPECT_CALL (foo, HandleDisconnect (_)).Times (0);
  EXPECT_CALL (foo, HandleDisconnect (barJid));
  EXPECT_CALL (bar, HandleDisconnect (_)).Times (0);

  ASSERT_TRUE (foo.Connect ());
  ASSERT_TRUE (bar.Connect ());

  /* Changing the nick should not be seen as disconnect.  */
  AccessRoom (foo).setNick ("my new nick");
  SleepSome ();

  bar.Disconnect ();
  SleepSome ();
  foo.Disconnect ();
}

/* ************************************************************************** */

using MucMessagingTests = MucClientTests;

TEST_F (MucMessagingTests, PublishMessages)
{
  const gloox::JID room = GetRoom ("foo");

  const auto fooJid = GetTestJid (0, "foo");
  TestClient foo(fooJid, GetPassword (0), room);
  ASSERT_TRUE (foo.Connect ());

  const auto barJid = GetTestJid (1, "bar");
  TestClient bar(barJid, GetPassword (1), room);
  ASSERT_TRUE (bar.Connect ());

  foo.Publish ("foo 1");
  bar.Publish ("bar 1");
  foo.Publish ("foo 2");
  bar.Publish ("bar 2");

  foo.ExpectMessages ({{barJid, "bar 1", false}, {barJid, "bar 2", false}});
  bar.ExpectMessages ({{fooJid, "foo 1", false}, {fooJid, "foo 2", false}});
}

TEST_F (MucMessagingTests, OtherRoom)
{
  const gloox::JID room1 = GetRoom ("room1");
  const gloox::JID room2 = GetRoom ("room2");

  const auto jid1 = GetTestJid (0, "foo");
  TestClient inRoom1(jid1, GetPassword (0), room1);
  ASSERT_TRUE (inRoom1.Connect ());

  const auto jid2 = GetTestJid (0, "bar");
  TestClient inRoom2(jid2, GetPassword (0), room1);
  ASSERT_TRUE (inRoom2.Connect ());

  const auto otherJid = GetTestJid (1, "other");
  TestClient other(otherJid, GetPassword (1), room2);
  ASSERT_TRUE (other.Connect ());

  other.Publish ("other");
  inRoom1.Publish ("in room");

  inRoom2.ExpectMessages ({{jid1, "in room", false}});
}

TEST_F (MucMessagingTests, PrivateMessages)
{
  const auto fooJid = GetTestJid (0, "foo");
  TestClient foo(fooJid, GetPassword (0), GetRoom ("foo"));
  ASSERT_TRUE (foo.Connect ());

  const auto barJid = GetTestJid (1, "bar");
  TestClient bar(barJid, GetPassword (1), GetRoom ("bar"));
  ASSERT_TRUE (bar.Connect ());

  foo.SendPrivate (barJid, "foo 1");
  bar.SendPrivate (fooJid, "bar 1");
  foo.SendPrivate (GetTestJid (1, "other res"), "invalid");
  foo.SendPrivate (barJid, "foo 2");
  bar.SendPrivate (fooJid, "bar 2");

  foo.ExpectMessages ({{barJid, "bar 1", true}, {barJid, "bar 2", true}});
  bar.ExpectMessages ({{fooJid, "foo 1", true}, {fooJid, "foo 2", true}});
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace democrit
