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

#include "game.hpp"

#include <glog/logging.h>

namespace dem
{

void
DemGame::SetupSchema (xaya::SQLiteDatabase& db)
{
  /* The data table that we need is really simple, as we just need to describe
     the map of executed trades (identified by btxid) to their confirmation
     height.  */
  db.Execute (R"(
    CREATE TABLE IF NOT EXISTS `trades` (
      `btxid` TEXT NOT NULL PRIMARY KEY,
      `height` INTEGER NOT NULL
    )
  )");
}

void
DemGame::GetInitialStateBlock (unsigned& height, std::string& hashHex) const
{
  const xaya::Chain chain = GetChain ();
  switch (chain)
    {
    case xaya::Chain::MAIN:
      height = 2'350'000;
      hashHex
          = "c66f30db579e0aad429648f4cb7dd67648d007ae4313f265a406b88f043b3d93";
      break;

    case xaya::Chain::TEST:
      height = 109'000;
      hashHex
          = "ebc9c179a6a9700777851d2b5452fa1c4b14aaa194a646e2a37cec8ca410e62a";
      break;

    case xaya::Chain::REGTEST:
      height = 0;
      hashHex
          = "6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1";
      break;

    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }
}

void
DemGame::InitialiseState (xaya::SQLiteDatabase& db)
{
  /* We start with an empty set of trades.  */
}

void
DemGame::ParseMove (const Json::Value& mv, std::string& btxid)
{
  btxid = mv["btxid"].asString ();
}

void
DemGame::UpdateState (xaya::SQLiteDatabase& db, const Json::Value& blockData)
{
  auto stmt = db.Prepare (R"(
    INSERT INTO `trades`
      (`btxid`, `height`)
      VALUES (?1, ?2)
  )");

  const unsigned height = blockData["block"]["height"].asUInt ();

  for (const auto& entry : blockData["moves"])
    {
      std::string btxid;
      ParseMove (entry, btxid);

      LOG (INFO) << "Finished trade btxid: " << btxid;

      stmt.Bind (1, btxid);
      stmt.Bind (2, height);
      stmt.Execute ();
      stmt.Reset ();
    }
}

Json::Value
DemGame::GetStateAsJson (const xaya::SQLiteDatabase& db)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `btxid`, `height`
      FROM `trades`
      ORDER BY `btxid`
  )");

  Json::Value res(Json::objectValue);
  while (stmt.Step ())
    {
      const auto btxid = stmt.Get<std::string> (0);
      const auto height = stmt.Get<int64_t> (1);
      res[btxid] = static_cast<Json::Int> (height);
    }

  return res;
}

DemGame::TradeData
DemGame::CheckTrade (const xaya::Game& g, const std::string& btxid)
{
  /* Checking the pending and confirmed state is done without locking the
     GSP in-between, so in theory there could be race conditions that change
     the state between the two lookups.  By checking the pending state first
     and the on-chain state second, we minimise the impact this has:

     If a pending move comes in between the two checks, then we will simply
     return "unknown" just as if we had locked the state immediately and not
     seen the pending move yet.

     If a block is attached, then we will (most likely) see the move already
     as pending but just not in the confirmed state, and thus return "pending".
     This is again just what would have happened with a full lock and/or
     if the RPC method had been called a tiny bit earlier.

     Only if a block is *detached* between the calls will there be an unexpected
     result:  Then the move is not in the pending state (because it was
     confirmed) but also no longer in the on-chain state, so that we return
     "unknown" even though the result should be "pending".  But this is a
     highly unlikely situation, and even then the result is not a big deal
     in practice.  */

  const Json::Value pending = g.GetPendingJsonState ()["pending"];
  const Json::Value confirmed = GetCustomStateData (g, "data",
      [&btxid] (const xaya::SQLiteDatabase& db) -> Json::Value
      {
        auto stmt = db.PrepareRo (R"(
          SELECT `height`
            FROM `trades`
            WHERE `btxid` = ?1
        )");
        stmt.Bind (1, btxid);

        if (!stmt.Step ())
          return Json::Value ();

        const auto height = stmt.Get<int> (0);
        CHECK (!stmt.Step ());

        return static_cast<Json::Int> (height);
      })["data"];

  CHECK (pending.isObject ());
  CHECK (confirmed.isNull () || confirmed.isUInt ());

  TradeData res;

  if (confirmed.asInt ())
    {
      res.state = TradeState::CONFIRMED;
      res.confirmationHeight = confirmed.asUInt ();
      return res;
    }

  if (pending.isMember (btxid))
    {
      res.state = TradeState::PENDING;
      return res;
    }

  res.state = TradeState::UNKNOWN;
  return res;
}

} // namespace dem
