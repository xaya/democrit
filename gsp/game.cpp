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

#include <sqlite3.h>

namespace dem
{

namespace
{

/**
 * Binds a string parameter in an SQLite prepared statement.
 */
void
BindString (sqlite3_stmt* stmt, const int pos, const std::string& val)
{
  CHECK_EQ (sqlite3_bind_text (stmt, pos, &val[0], val.size (),
                               SQLITE_TRANSIENT),
            SQLITE_OK);
}

/**
 * Retrieves a column value from an SQLite statement as string.
 */
std::string
GetStringColumn (sqlite3_stmt* stmt, const int pos)
{
  const unsigned char* str = sqlite3_column_text (stmt, pos);
  return reinterpret_cast<const char*> (str);
}

} // anonymous namespace

void
DemGame::SetupSchema (xaya::SQLiteDatabase& db)
{
  /* The data table that we need is really simple, as we just need to describe
     the set of executed trades.  Each trade is identified by the seller's name
     (who sent the move) and the seller-chosen ID string for it.  IDs are
     assumed to be unique per seller name, but not forced to be so.  */
  auto* stmt = db.Prepare (R"(
    CREATE TABLE IF NOT EXISTS `trades` (
      `seller_name` TEXT,
      `seller_id` TEXT,
      PRIMARY KEY (`seller_name`, `seller_id`)
    )
  )");
  CHECK_EQ (sqlite3_step (stmt), SQLITE_DONE);
}

void
DemGame::GetInitialStateBlock (unsigned& height, std::string& hashHex) const
{
  const xaya::Chain chain = GetChain ();
  switch (chain)
    {
    case xaya::Chain::MAIN:
      height = 1'700'000;
      hashHex
          = "5792ddec8d414bbde8264bf67002215014c8432a6dc083b71fed0feffd5638b3";
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

bool
DemGame::ParseMove (const Json::Value& mv, std::string& name,
                    std::string& tradeId)
{
  name = mv["name"].asString ();
  const auto& mvData = mv["move"];

  if (!mvData.isObject ())
    return false;

  const auto& tradeIdVal = mvData["t"];
  if (!tradeIdVal.isString ())
    return false;
  tradeId = tradeIdVal.asString ();

  return true;
}

void
DemGame::UpdateState (xaya::SQLiteDatabase& db, const Json::Value& blockData)
{
  auto* stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `trades`
      (`seller_name`, `seller_id`)
      VALUES (?1, ?2)
  )");

  for (const auto& entry : blockData["moves"])
    {
      std::string name, tradeId;
      if (!ParseMove (entry, name, tradeId))
        {
          LOG (WARNING) << "Invalid move data: " << entry;
          continue;
        }

      LOG (INFO)
          << "Finished trade:\n"
          << "  Transaction: " << entry["txid"].asString () << "\n"
          << "  Seller name: " << name << "\n"
          << "  Seller ID: " << tradeId;

      sqlite3_reset (stmt);
      BindString (stmt, 1, name);
      BindString (stmt, 2, tradeId);
      CHECK_EQ (sqlite3_step (stmt), SQLITE_DONE);
    }
}

Json::Value
DemGame::GetStateAsJson (const xaya::SQLiteDatabase& db)
{
  auto* stmt = db.PrepareRo (R"(
    SELECT `seller_name`, `seller_id`
      FROM `trades`
      ORDER BY `seller_name`, `seller_id`
  )");

  Json::Value res(Json::objectValue);
  while (true)
    {
      const int rc = sqlite3_step (stmt);
      if (rc == SQLITE_DONE)
        break;
      CHECK_EQ (rc, SQLITE_ROW);

      const std::string name = GetStringColumn (stmt, 0);
      const std::string tradeId = GetStringColumn (stmt, 1);

      if (!res.isMember (name))
        res[name] = Json::Value (Json::arrayValue);
      res[name].append (tradeId);
    }

  return res;
}

} // namespace dem
