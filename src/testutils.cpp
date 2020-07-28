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

#include "testutils.hpp"

#include <chrono>
#include <thread>

namespace democrit
{

namespace
{

/**
 * Configuration for the local test environment.
 */
const ServerConfiguration LOCAL_SERVER =
  {
    "localhost",
    "muc.localhost",
    {
      {"xmpptest1", "password"},
      {"xmpptest2", "password"},
    },
  };

} // anonymous namespace

const ServerConfiguration&
GetServerConfig ()
{
  return LOCAL_SERVER;
}

gloox::JID
GetTestJid (const unsigned n)
{
  const auto& cfg = GetServerConfig ();

  gloox::JID res;
  res.setUsername (cfg.accounts[n].name);
  res.setServer (cfg.server);

  return res;
}

std::string
GetPassword (const unsigned n)
{
  return GetServerConfig ().accounts[n].password;
}

gloox::JID
GetRoom (const std::string& nm)
{
  gloox::JID res;
  res.setUsername (nm);
  res.setServer (GetServerConfig ().muc);

  return res;
}

void
SleepSome ()
{
  std::this_thread::sleep_for (std::chrono::milliseconds (10));
}

} // namespace democrit
