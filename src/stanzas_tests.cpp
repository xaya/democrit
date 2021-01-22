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

#include "private/stanzas.hpp"

#include "testutils.hpp"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <google/protobuf/util/message_differencer.h>

namespace democrit
{
namespace
{

using google::protobuf::util::MessageDifferencer;

/**
 * Runs a basic roundtrip test for a ProtoStanza of the given type.
 * It parses the proto text format, constructs a stanza from it,
 * serialises it to a tag, parses it back into a stanza, clones it, and then
 * checks that the recovered stanza's proto matches the initial one.
 */
template <typename T>
  void
  ProtoStanzaRoundtrip (const std::string& str)
{
  const auto data = ParseTextProto<typename T::ProtoType> (str);

  const T original(data);
  ASSERT_TRUE (original.IsValid ());

  std::unique_ptr<gloox::Tag> tag(original.tag ());
  ASSERT_EQ (tag->name (), T::TAG);
  ASSERT_EQ (tag->xmlns (), T::XMLNS);

  std::unique_ptr<gloox::StanzaExtension> parsed(
      original.newInstance (tag.get ()));

  std::unique_ptr<T> recovered(dynamic_cast<T*> (parsed->clone ()));
  ASSERT_NE (recovered, nullptr);
  ASSERT_TRUE (recovered->IsValid ());

  ASSERT_TRUE (MessageDifferencer::Equals (recovered->GetData (), data));
}

using StanzasTests = testing::Test;

TEST_F (StanzasTests, InvalidData)
{
  auto tag = charon::EncodeXmlPayload ("orders", "invalid proto");
  tag->setXmlns (AccountOrdersStanza::XMLNS);

  AccountOrdersStanza stanza(*tag);
  EXPECT_FALSE (stanza.IsValid ());
}

TEST_F (StanzasTests, AccountOrdersStanza)
{
  ProtoStanzaRoundtrip<AccountOrdersStanza> (R"(
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
  )");
}

TEST_F (StanzasTests, ProcessingMessageStanza)
{
  ProtoStanzaRoundtrip<ProcessingMessageStanza> (R"(
    identifier: "me\n42"
    psbt: { psbt: "abc" }
  )");
}

} // anonymous namespace
} // namespace democrit
