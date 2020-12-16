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

#include "mockxaya.hpp"

#include <jsonrpccpp/common/exception.h>

#include <sstream>

namespace democrit
{

int
GetPortForMockServer ()
{
  static unsigned cnt = 0;
  ++cnt;

  return 2'000 + (cnt % 1'000);
}

std::string
MockXayaRpcServer::getnewaddress ()
{
  ++addrCount;

  std::ostringstream res;
  res << "addr " << addrCount;

  return res.str ();
}

Json::Value
MockXayaRpcServer::name_show (const std::string& name)
{
  if (name == "p/invalid" || name.substr (0, 2) != "p/")
    throw jsonrpc::JsonRpcException (-4, "name not found");

  const std::string suffix = name.substr (2);

  Json::Value res(Json::objectValue);
  res["name"] = suffix;
  res["txid"] = suffix + " txid";
  res["vout"] = 12;

  return res;
}

} // namespace democrit
