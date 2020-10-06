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

#ifndef DEMOCRIT_AUTHENTICATOR_HPP
#define DEMOCRIT_AUTHENTICATOR_HPP

#include <gloox/jid.h>

#include <set>
#include <string>

namespace democrit
{

/**
 * Helper class to "authenticate" Xaya accounts from their XMPP identity.
 * In particular, we have a list of XMPP servers / domains that we trust
 * to run XID authentication.  For any JID from those servers, we then
 * see if we can decode the username into a Xaya account.
 */
class Authenticator
{

private:

  /** The set of trusted servers.  */
  const std::set<std::string> xidServers;

  /**
   * Constructs an instance with the list of servers extracted from
   * a comma-separated list of strings.
   */
  explicit Authenticator (const std::string& servers);

  friend class AuthenticatorTests;

public:

  /**
   * Constructs an instance with a default set of servers, which is
   * based on command-line arguments.
   */
  Authenticator ();

  Authenticator (const Authenticator&) = delete;
  void operator= (const Authenticator&) = delete;

  /**
   * Tries to authenticate a given JID.  Returns true and sets the
   * account string on success (we believe the JID corresponds to the
   * given Xaya account) and false if we failed to do so.
   */
  bool Authenticate (const gloox::JID& jid, std::string& account) const;

};

} // namespace democrit

#endif // DEMOCRIT_AUTHENTICATOR_HPP
