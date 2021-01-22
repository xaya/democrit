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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <sstream>

namespace democrit
{

DEFINE_string (democrit_xid_servers, "chat.xaya.io",
               "Comma-separated list of XMPP servers that we trust"
               " to apply XID authentication");

namespace
{

/**
 * Parses a comma-separated string into pieces.
 */
std::set<std::string>
ParseCommaSeparated (const std::string& lst)
{
  if (lst.empty ())
    return {};

  std::set<std::string> res;
  std::istringstream in(lst);
  while (in.good ())
    {
      std::string cur;
      std::getline (in, cur, ',');
      res.insert (cur);
    }

  return res;
}

/**
 * Returns true if the character is "simple" (lower alphanumeric) for the
 * purpose of encoding the name.
 */
bool
IsSimpleChar (const char c)
{
  if (c >= '0' && c <= '9')
    return true;
  if (c >= 'a' && c <= 'z')
    return true;
  return false;
}

/**
 * Returns the value as hex character if it is valid, and -1 if it is not
 * a valid (lower-case) hex digit for the purpose of name encodings.
 */
int
HexCharValue (const char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  return -1;
}

/** Prefix for hex encoded names.  */
const std::string ENCODED_PREFIX = "x-";

/**
 * Decodes an encoded XMPP name to the underlying Xaya name.
 */
bool
DecodeName (const std::string& name, std::string& decoded)
{
  /* Empty strings have to be hex encoded.  */
  if (name.empty ())
    return false;

  /* See if this is a simple name.  */
  if (name.substr (0, ENCODED_PREFIX.size ()) != ENCODED_PREFIX)
    {
      for (const char c : name)
        if (!IsSimpleChar (c))
          return false;

      decoded = name;
      return true;
    }

  const auto hexPart = name.substr (ENCODED_PREFIX.size ());
  if (hexPart.size () % 2 != 0)
    return false;

  /* The empty string hex-encoded is valid, and a special case (because it
     is fine to not have any non-simple characters in it).  */
  if (hexPart.empty ())
    {
      decoded = "";
      return true;
    }

  std::ostringstream out;
  uint8_t cur = 0;
  bool inByte = false;
  bool foundNonSimple = false;
  for (const char c : hexPart)
    {
      const int val = HexCharValue (c);
      if (val == -1)
        return false;
      CHECK_GE (val, 0);
      CHECK_LT (val, 16);

      cur |= val;
      if (inByte)
        {
          const char decodedChar = static_cast<char> (cur);
          if (!IsSimpleChar (decodedChar))
            foundNonSimple = true;

          out << decodedChar;
          cur = 0;
          inByte = false;
        }
      else
        {
          cur <<= 4;
          inByte = true;
        }
    }
  CHECK (!inByte);
  CHECK_EQ (cur, 0);

  /* Names that are all-simple must not be hex encoded, to prevent
     multiple XMPP names to resolve to the same decoded one.  */
  if (!foundNonSimple)
    return false;

  decoded = out.str ();
  return true;
}

} // anonymous namespace

Authenticator::Authenticator (const std::string& servers)
  : xidServers(ParseCommaSeparated (servers))
{}

Authenticator::Authenticator ()
  : xidServers(ParseCommaSeparated (FLAGS_democrit_xid_servers))
{}

bool
Authenticator::Authenticate (const gloox::JID& jid, std::string& account) const
{
  if (xidServers.count (jid.server ()) == 0)
    return false;

  if (!DecodeName (jid.username (), account))
    return false;

  VLOG (1) << "JID for account " << account << ": " << jid.full ();
  knownJids[account] = jid;
  return true;
}

bool
Authenticator::LookupJid (const std::string& account, gloox::JID& jid) const
{
  const auto mit = knownJids.find (account);
  if (mit == knownJids.end ())
    return false;

  jid = mit->second;
  return true;
}

} // namespace democrit
