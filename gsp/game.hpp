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

#ifndef DEMOCRIT_GSP_GAME_HPP
#define DEMOCRIT_GSP_GAME_HPP

#include "pending.hpp"

#include <xayagame/game.hpp>
#include <xayagame/sqlitegame.hpp>

#include <json/json.h>

#include <string>

namespace dem
{

/**
 * SQLiteGame instance for the Democrit GSP (that just tracks pending / executed
 * trades based on the btxid).
 */
class DemGame : public xaya::SQLiteGame
{

private:

  /**
   * Parses a move from the notification JSON object.
   * This is also used for pending moves.
   */
  static void ParseMove (const Json::Value& mv, std::string& btxid);

  friend class dem::PendingMoves;

protected:

  void SetupSchema (xaya::SQLiteDatabase& db) override;
  void GetInitialStateBlock (unsigned& height,
                             std::string& hashHex) const override;

  void InitialiseState (xaya::SQLiteDatabase& db) override;
  void UpdateState (xaya::SQLiteDatabase& db,
                    const Json::Value& blockData) override;

  Json::Value GetStateAsJson (const xaya::SQLiteDatabase& db) override;

public:

  /**
   * Possible state of a trade.
   */
  enum class TradeState
  {
    /** The trade has not been seen anywhere on the network yet.  */
    UNKNOWN,
    /** The trade's atomic transaction is pending in the mempool.  */
    PENDING,
    /** The trade's atomic transaction has been confirmed.  */
    CONFIRMED,
  };

  /**
   * Data for a particular trade.
   */
  struct TradeData
  {

    /** The state of the trade.  */
    TradeState state;

    /** If the trade is confirmed, the height at which it was confirmed.  */
    unsigned confirmationHeight;

  };

  /**
   * Queries for the state of the trade with given btxid.
   */
  TradeData CheckTrade (const xaya::Game& g, const std::string& btxid);

};

} // namespace dem

#endif // DEMOCRIT_GSP_GAME_HPP
