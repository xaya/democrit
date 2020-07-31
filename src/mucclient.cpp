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
  /* Make sure to clean up any still lingering disconnecter thread (which
     has probably finished executing, but is still not joined).  */
  if (disconnecter.joinable ())
    disconnecter.join ();
  disconnecting = false;

  if (!XmppClient::Connect (-1))
    return false;

  std::unique_lock<std::mutex> lock(mut);
  nickToJid.clear ();

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
  if (disconnecting)
    return;

  if (disconnecter.joinable ())
    disconnecter.join ();

  std::lock_guard<std::mutex> lock(mut);
  nickToJid.clear ();

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

  if (disconnecter.joinable ())
    disconnecter.join ();

  std::lock_guard<std::mutex> lock(mut);
  CHECK (!disconnecting);
  CHECK (room == nullptr);
  CHECK (!XmppClient::IsConnected ());
}

bool
MucClient::ResolveNickname (const std::string& nick, gloox::JID& jid) const
{
  std::lock_guard<std::mutex> lock(mut);

  const auto mit = nickToJid.find (nick);

  if (mit == nickToJid.end ())
    return false;

  jid = mit->second;
  return true;
}

void
MucClient::RegisterExtension (std::unique_ptr<gloox::StanzaExtension> ext)
{
  RunWithClient ([&ext] (gloox::Client& c)
    {
      c.registerStanzaExtension (ext.release ());
    });
}

void
MucClient::PublishMessage (ExtensionData&& ext)
{
  CHECK (IsConnected ());

  gloox::Message msg(gloox::Message::Groupchat, roomName);
  for (auto& entry : ext)
    msg.addExtension (entry.release ());

  RunWithClient ([&msg] (gloox::Client& c)
    {
      c.send (msg);
    });
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
  VLOG (1)
      << "Presence for " << participant.jid->full ()
      << " with flags " << participant.flags
      << " on room " << room->name ()
      << ": " << presence.presence ();

  /* Nick changes also send an unavailable presence.  We want to not consider
     them as such, though.  */
  bool unavailable = (presence.presence () == gloox::Presence::Unavailable);
  if (participant.flags & gloox::UserNickChanged)
    unavailable = false;

  /* If this is for self, handle a potential successful join or us being
     removed from the room.  */
  if (participant.flags & gloox::UserSelf)
    {
      if (unavailable)
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

  LOG_IF (WARNING, participant.jid == nullptr)
      << "Did not receive full JID for " << participant.nick->full ();

  /* If someone left the room, just clear their nick-map entry.  */
  if (unavailable)
    {
      std::lock_guard<std::mutex> lock(mut);

      VLOG (1)
          << "Removing nick-map entry for " << participant.nick->resource ();
      nickToJid.erase (participant.nick->resource ());

      if (participant.jid != nullptr)
        {
          VLOG (1)
              << "Room participant " << participant.jid->full ()
              << " is now disconnected";
          HandleDisconnect (*participant.jid);
        }

      return;
    }

  /* If we do not know the full JID, nothing can be done.  */
  if (participant.jid == nullptr)
    return;

  /* Otherwise, update or insert the nick-map entry.  */
  std::lock_guard<std::mutex> lock(mut);

  std::string nick;
  if (participant.flags & gloox::UserNickChanged)
    {
      nickToJid.erase (participant.nick->resource ());
      nick = participant.newNick;
    }
  else
    nick = participant.nick->resource ();
  CHECK_NE (nick, "");

  LOG (INFO)
      << "Full jid for " << nick << " in room " << room->name ()
      << ": " << participant.jid->full ();
  nickToJid[nick] = *participant.jid;
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
  CHECK_EQ (msg.from ().bareJID (), roomName);

  gloox::JID realJid;
  if (ResolveNickname (msg.from ().resource (), realJid))
    HandleMessage (realJid, msg);
  else
    {
      /* A side effect of how we handle nicknames is that we do not know
         our own, which means that we filter out in particular our own
         messages relayed back to us here.  */
      VLOG (1)
          << "Ignoring message from " << msg.from ().full ()
          << " whose real sender JID we do not know";
    }
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
