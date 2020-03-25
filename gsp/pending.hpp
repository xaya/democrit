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

#ifndef DEMOCRIT_GSP_PENDING_HPP
#define DEMOCRIT_GSP_PENDING_HPP

#include <xayagame/pendingmoves.hpp>

#include <json/json.h>

namespace dem
{

/**
 * Tracker for pending moves in the Democrit GSP.
 */
class PendingMoves : public xaya::PendingMoveProcessor
{

private:

  /**
   * The current pending state.  It is already in the JSON form for simplicity.
   * We can still look up by name efficiently, and then just need to iterate
   * through all open trades of a single name (which shouldn't be many anyway).
   */
  Json::Value pending;

protected:

  void Clear () override;
  void AddPendingMove (const Json::Value& mv) override;

public:

  PendingMoves ()
    : pending(Json::objectValue)
  {}

  Json::Value ToJson () const override;

};

} // namespace dem

#endif // DEMOCRIT_GSP_PENDING_HPP
