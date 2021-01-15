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

#ifndef DEMOCRIT_AUTHENTICATOR_HPP
#define DEMOCRIT_AUTHENTICATOR_HPP

#include <gloox/jid.h>

#include <set>
#include <string>
#include <unordered_map>

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
   * In-memory map of accounts to JIDs that have been authenticated
   * successfully.  We use this to lookup the JID for sending messages back,
   * and knowing which server (as well as resource and all that)
   * they were using when sending to us.
   */
  mutable std::unordered_map<std::string, gloox::JID> knownJids;

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

  /**
   * Finds the JID corresponding to a Xaya account (i.e. the reverse of
   * Authenticate).  Since we do not know which of the servers the account
   * may be using, we do not simply encode the name back.  Instead, we have
   * an in-memory map of all JIDs that were successfully authenticated
   * previously, and then use that map to look up the JID for the authenticated
   * account.  During normal operation, we will always receive messages from
   * some account first before we need to send private messages to them.
   *
   * The method returns false if we are unable to locate the correct JID,
   * i.e. because it has not sent a message before to us.
   */
  bool LookupJid (const std::string& account, gloox::JID& jid) const;

};

} // namespace democrit

#endif // DEMOCRIT_AUTHENTICATOR_HPP
