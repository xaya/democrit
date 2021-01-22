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

#include "private/authenticator.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace democrit
{

class AuthenticatorTests : public testing::Test
{

private:

  /** The authenticator instance we use.  */
  std::unique_ptr<Authenticator> auth;

protected:

  /**
   * Sets the list of acceptable servers.
   */
  void
  SetServers (const std::string& lst)
  {
    auth.reset (new Authenticator (lst));
  }

  /**
   * Expects the JID (parsed from string) to be invalid.
   */
  void
  ExpectInvalid (const std::string& jid)
  {
    std::string decoded;
    ASSERT_FALSE (auth->Authenticate (gloox::JID (jid), decoded))
        << "Expected to be invalid: " << jid;
  }

  /**
   * Expects the JID (parsed from string) to be valid and result
   * in the given decoded name.
   */
  void
  ExpectValid (const std::string& jid, const std::string& expected)
  {
    std::string decoded;
    ASSERT_TRUE (auth->Authenticate (gloox::JID (jid), decoded))
        << "Expected to be valid: " << jid;
    EXPECT_EQ (decoded, expected);
  }

  /**
   * Exposes the authenticator instance to tests.
   */
  const Authenticator&
  GetAuth () const
  {
    return *auth;
  }

};

namespace
{

TEST_F (AuthenticatorTests, EmptyServerList)
{
  SetServers ("");
  ExpectInvalid ("domob@chat.xaya.io");
  ExpectInvalid ("domob@");
}

TEST_F (AuthenticatorTests, OneServer)
{
  SetServers ("chat.xaya.io");
  ExpectInvalid ("domob@example.com");
  ExpectValid ("domob@chat.xaya.io", "domob");
}

TEST_F (AuthenticatorTests, TwoServers)
{
  SetServers ("chat.xaya.io,localhost");
  ExpectInvalid ("domob@example.com");
  ExpectValid ("domob@chat.xaya.io", "domob");
  ExpectValid ("daniel@localhost", "daniel");
}

TEST_F (AuthenticatorTests, InvalidDecoding)
{
  SetServers ("server");

  ExpectInvalid ("@server");

  ExpectInvalid ("domob foobar@server");
  ExpectInvalid ("abc.def@server");
  ExpectInvalid ("no-dash@server");
  ExpectInvalid ("dom\nob@server");
  ExpectInvalid (u8"äöü@server");

  ExpectInvalid ("x-x");
  ExpectInvalid ("x-a");
  ExpectInvalid ("x-2D");
  ExpectInvalid ("x-\nabc");

  ExpectInvalid ("x-616263");
}

TEST_F (AuthenticatorTests, JidConversionLowercases)
{
  SetServers ("server");
  ExpectValid ("Abc@server", "abc");
}

TEST_F (AuthenticatorTests, SimpleNames)
{
  SetServers ("server");

  ExpectValid ("domob@server", "domob");
  ExpectValid ("0@server", "0");
  ExpectValid ("foo42bar@server", "foo42bar");
  ExpectValid ("xxx@server", "xxx");
}

TEST_F (AuthenticatorTests, HexEncodedNames)
{
  SetServers ("server");

  ExpectValid ("x-@server", "");
  ExpectValid ("x-782d666f6f@server", "x-foo");
  ExpectValid ("x-c3a4c3b6c3bc@server", u8"äöü");
  ExpectValid ("x-466f6f20426172@server", "Foo Bar");
}

TEST_F (AuthenticatorTests, LookupJid)
{
  SetServers ("server1,server2");

  gloox::JID jid;
  EXPECT_FALSE (GetAuth ().LookupJid ("domob", jid));
  EXPECT_FALSE (GetAuth ().LookupJid (u8"äöü", jid));

  ExpectValid ("domob@server1/foo", "domob");
  ExpectValid ("x-c3a4c3b6c3bc@server2/bar", u8"äöü");

  ASSERT_TRUE (GetAuth ().LookupJid ("domob", jid));
  EXPECT_EQ (jid, "domob@server1/foo");
  ASSERT_TRUE (GetAuth ().LookupJid (u8"äöü", jid));
  EXPECT_EQ (jid, "x-c3a4c3b6c3bc@server2/bar");
  ASSERT_FALSE (GetAuth ().LookupJid ("abc", jid));
}

} // anonymous namespace
} // namespace democrit
