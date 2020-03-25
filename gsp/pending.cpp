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

#include "pending.hpp"

#include "game.hpp"

#include <glog/logging.h>

namespace dem
{

void
PendingMoves::Clear ()
{
  pending = Json::Value (Json::objectValue);
}

Json::Value
PendingMoves::ToJson () const
{
  return pending;
}

void
PendingMoves::AddPendingMove (const Json::Value& mv)
{
  std::string name, tradeId;
  if (!DemGame::ParseMove (mv, name, tradeId))
    {
      LOG (WARNING) << "Invalid pending move: " << mv;
      return;
    }

  if (!pending.isMember (name))
    pending[name] = Json::Value (Json::arrayValue);
  auto& arr = pending[name];
  CHECK (arr.isArray ());

  for (const auto& existing : arr)
    if (existing.asString () == tradeId)
      return;
  arr.append (tradeId);
}

} // namespace dem
