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

#include "testutils.hpp"

#include <experimental/filesystem>

#include <chrono>
#include <sstream>
#include <thread>

namespace democrit
{

namespace fs = std::experimental::filesystem;

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
      {"xmpptest3", "password"},
    },
  };

} // anonymous namespace

const ServerConfiguration&
GetServerConfig ()
{
  return LOCAL_SERVER;
}

std::string
GetTestCA ()
{
#ifndef CHARON_PREFIX
#error Charon installation prefix not defined
#endif // CHARON_PREFIX
  return fs::path (CHARON_PREFIX) / "share" / "charon" / "testenv.pem";
}

gloox::JID
GetTestJid (const unsigned n, const std::string& res)
{
  const auto& cfg = GetServerConfig ();

  gloox::JID jid;
  jid.setUsername (cfg.accounts[n].name);
  jid.setServer (cfg.server);
  jid.setResource (res);

  return jid;
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

Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  return res;
}

constexpr const char* TestAssets::GAME_ID;

std::string
TestAssets::GetGameId () const
{
  return GAME_ID;
}

bool
TestAssets::IsAsset (const Asset& asset) const
{
  return asset == "gold" || asset == "silver" || asset == "bronze";
}

bool
TestAssets::CanSell (const std::string& name, const Asset& asset,
                     const Amount n, xaya::uint256& hash) const
{
  const auto mitAccount = balances.find (name);
  if (mitAccount == balances.end ())
    return false;

  const auto& account = mitAccount->second;
  const auto mitAsset = account.find (asset);
  if (mitAsset == account.end ())
    return false;

  hash = currentHash;
  return n <= mitAsset->second;
}

bool
TestAssets::CanBuy (const std::string& name, const Asset& asset,
                    const Amount n) const
{
  return balances.count (name) > 0;
}

Json::Value
TestAssets::GetTransferMove (const std::string& sender,
                             const std::string& receiver,
                             const Asset& asset, const Amount n) const
{
  Json::Value res(Json::objectValue);
  res["to"] = receiver;
  res["asset"] = asset;
  res["amount"] = static_cast<Json::Int64> (n);
  return res;
}

} // namespace democrit
