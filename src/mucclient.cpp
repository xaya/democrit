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

#include "private/mucclient.hpp"

#include <xayautil/cryptorand.hpp>

#include <glog/logging.h>

namespace democrit
{

MucClient::~MucClient ()
{
  Disconnect ();
  CHECK (room == nullptr);
}

bool
MucClient::Connect ()
{
  std::unique_lock<std::mutex> lock(mut);

  /* Make sure to clean up any still lingering disconnecter thread (which
     has probably finished executing, but is still not joined).  */
  if (disconnecter.joinable ())
    disconnecter.join ();
  disconnecting = false;

  if (!XmppClient::Connect (-1))
    return false;

  CHECK (room == nullptr) << "Did not fully disconnect previously";

  /* The nick names in the room are not used for anything, as they will be
     mapped to full JIDs instead for any communication.  But they have to be
     unique in order to avoid failures when joining.  Thus we simply use
     a random value, which will be (almost) guaranteed to be unique.  */
  xaya::CryptoRand rnd;
  const auto nick = rnd.Get<xaya::uint256> ();
  gloox::JID roomJid = roomName;
  roomJid.setResource (nick.ToHex ());

  joining = true;
  gloox::MUCRoomHandler* handler = this;
  RunWithClient ([&] (gloox::Client& c)
    {
      LOG (INFO) << "Attempting to join room " << roomJid.full ();
      room = std::make_unique<gloox::MUCRoom> (&c, roomJid, handler);
      room->join ();
    });

  while (joining)
    cvJoin.wait (lock);

  /* If an error occurs while joining, we get disconnected before
     the joining wait is notified.  */
  return IsConnected ();
}

void
MucClient::DisconnectAsync ()
{
  std::lock_guard<std::mutex> lock(mut);

  if (disconnecting)
    return;

  if (disconnecter.joinable ())
    disconnecter.join ();

  disconnecting = true;
  std::thread worker([this] ()
    {
      if (room != nullptr)
        {
          LOG (INFO) << "Leaving room " << room->name ();
          room->leave ();
        }

      /* Disconnect first, and then destroy the room.  This ensures
         it won't be accessed after being freed.  */
      XmppClient::Disconnect ();
      room.reset ();

      disconnecting = false;
    });

  disconnecter = std::move (worker);
}

void
MucClient::Disconnect ()
{
  DisconnectAsync ();

  std::lock_guard<std::mutex> lock(mut);
  if (disconnecter.joinable ())
    disconnecter.join ();

  CHECK (!disconnecting);
  CHECK (room == nullptr);
  CHECK (!XmppClient::IsConnected ());
}

bool
MucClient::handleMUCRoomCreation (gloox::MUCRoom* r)
{
  CHECK_EQ (r, room.get ());
  LOG (WARNING) << "Creating non-existing MUC room " << roomName.full ();
  return true;
}

void
MucClient::handleMUCParticipantPresence (
    gloox::MUCRoom* r, const gloox::MUCRoomParticipant participant,
    const gloox::Presence& presence)
{
  CHECK_EQ (r, room.get ());
  LOG (INFO)
      << "Presence for " << participant.jid->full ()
      << " with flags " << participant.flags
      << " on room " << room->name ();

  /* If this is for self, handle a potential successful join or us being
     removed from the room.  */
  if (participant.flags & gloox::UserSelf)
    {
      if (presence.presence () == gloox::Presence::Unavailable)
        {
          LOG (WARNING) << "We have been disconnected from " << room->name ();
          DisconnectAsync ();
        }

      std::lock_guard<std::mutex> lock(mut);
      if (joining)
        {
          joining = false;
          cvJoin.notify_all ();
        }

      return;
    }

  /* FIXME: Handle others' presences (for nick-to-JID mapping).  */
}

void
MucClient::handleMUCMessage (gloox::MUCRoom* r, const gloox::Message& msg,
                             const bool priv)
{
  CHECK_EQ (r, room.get ());

  if (priv)
    {
      LOG (WARNING)
          << "Ignoring private message on room " << room->name ()
          << " from " << msg.from ().full ();
      return;
    }

  VLOG (1)
      << "Received message from " << msg.from ().full ()
      << " on room " << room->name ();
  /* FIXME: Look up the real JID of the sender and pass it.  */
  HandleMessage (msg);
}

void
MucClient::handleMUCError (gloox::MUCRoom* r, const gloox::StanzaError error)
{
  CHECK_EQ (r, room.get ());

  LOG (WARNING)
      << "Received error for MUC room " << room->name () << ": " << error;
  DisconnectAsync ();

  std::lock_guard<std::mutex> lock(mut);
  if (joining)
    {
      joining = false;
      cvJoin.notify_all ();
    }
}

} // namespace democrit
