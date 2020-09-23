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

#ifndef DEMOCRIT_JSON_HPP
#define DEMOCRIT_JSON_HPP

#include <json/json.h>

namespace democrit
{

/**
 * Converts one of the Democrit protocol buffers into a JSON form.
 * This is implemented for the protos that are part of the public interface
 * of Daemon, and can be used to build a JSON-RPC interface for Daemon.
 */
template <typename Proto>
  Json::Value ProtoToJson (const Proto& pb);

/**
 * Tries to convert a JSON representation into the corresponding protocol
 * buffer message.  This is implemented for protos that are used as inputs
 * into Daemon, e.g. Order.  It can be used to build a JSON-RPC interface
 * around Daemon.
 *
 * The method returns true on success (the JSON format was valid) and fills
 * in the output proto.
 */
template <typename Proto>
  bool ProtoFromJson (const Json::Value& val, Proto& pb);

} // namespace democrit

#endif // DEMOCRIT_JSON_HPP
