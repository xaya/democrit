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

#ifndef DEMOCRIT_MUCCLIENT_HPP
#define DEMOCRIT_MUCCLIENT_HPP

#include <charon/xmppclient.hpp>

#include <gloox/jid.h>
#include <gloox/message.h>
#include <gloox/mucroom.h>
#include <gloox/mucroomhandler.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace democrit
{

/**
 * The main XMPP client class used in Democrit.  It wraps around Charon's
 * basic XmppClient (based on gloox) and adds in MUC functionality as needed
 * for Democrit.  The client joins a pre-defined room, and then handles
 * message broadcasts as well as private messages (although we use
 * direct XMPP messages to the full JID instead of in-room private messages
 * for that).  The MucClient also takes care of mapping in-room nick names
 * to real JIDs, which we can then "soft rely on" as being authenticated
 * through XID.
 */
class MucClient : private charon::XmppClient,
                  private gloox::MUCRoomHandler,
                  private gloox::MessageHandler
{

public:

  /**
   * A list of stanza extensions that can be published to the MUC channel.
   */
  using ExtensionData = std::vector<std::unique_ptr<gloox::StanzaExtension>>;

private:

  friend class MucClientTests;

  /** The name of the room (including the server) to join on connecting.  */
  const gloox::JID roomName;

  /** The gloox MUC room handle (while connected).  */
  std::unique_ptr<gloox::MUCRoom> room;

  /**
   * It may happen that we need to disconnect in response to a callback, e.g.
   * when we receive some error or get removed from the room.  In this case
   * we have to call Disconnect asynchronously (as doing it from inside
   * a gloox handler would result in a deadlock).  If we are currently
   * disconnecting in this way, this holds the thread instance doing the
   * call to disconnect.
   */
  std::thread disconnecter;

  /** True if we are still in the progress of async disconnecting.  */
  std::atomic<bool> disconnecting;

  /**
   * Condition variable used to notify the thread waiting for complete
   * join of the room when we receive our own presence (or when an error
   * occurred and we are disconnected instead).
   */
  std::condition_variable cvJoin;

  /**
   * Flag that indicates we are currently joining the room and still waiting
   * for either our own presence (in which case it worked) or an error
   * (in which case we disconnect again).
   */
  bool joining;

  /**
   * Maps in-room nicknames to the corresponding full JIDs.  We need that
   * so that we can know who a MUC message was "really" from, as we use
   * the full JIDs (which e.g. may be XID-authenticated by the server)
   * for identifying room participants.
   */
  std::map<std::string, gloox::JID> nickToJid;

  /**
   * Mutex used to lock for this instance (in particular, for syncing the
   * joining / disconnecting things).
   */
  mutable std::mutex mut;

  /**
   * Disconnect asynchronously.  This can be done also from inside
   * gloox handlers.  The function will return immediately, but will
   * start a disconnect in the disconnecter thread.
   */
  void DisconnectAsync ();

  /**
   * Resolves an in-room nick name to the corresponding full JID.
   * Returns false if we do not know that nick.
   */
  bool ResolveNickname (const std::string& nick, gloox::JID& jid) const;

  /**
   * Adds the given stanza extensions to the message and then broadcasts it
   * using normal "send" functionality.  This code is shared between sending
   * public and private messages.
   */
  void SendMessage (gloox::Message&& msg, ExtensionData&& ext);

  void handleMUCError (gloox::MUCRoom* r, gloox::StanzaError) override;
  bool handleMUCRoomCreation (gloox::MUCRoom* r) override;
  void handleMUCMessage (gloox::MUCRoom* r, const gloox::Message& msg,
                         bool priv) override;
  void handleMUCParticipantPresence (
      gloox::MUCRoom* r, gloox::MUCRoomParticipant participant,
      const gloox::Presence& presence) override;

  void handleMessage (const gloox::Message& msg,
                      gloox::MessageSession* session) override;

  void
  handleMUCSubject (gloox::MUCRoom* r, const std::string& nick,
                    const std::string& subject) override
  {}

  void
  handleMUCInviteDecline (gloox::MUCRoom* r, const gloox::JID& invitee,
                          const std::string& reason) override
  {}

  void
  handleMUCInfo (gloox::MUCRoom* r, const int features,
                 const std::string& name,
                 const gloox::DataForm* infoForm) override
  {}

  void
  handleMUCItems (gloox::MUCRoom* r,
                  const gloox::Disco::ItemList& items) override
  {}

protected:

  /**
   * Handler called for all published messages (not including private ones)
   * on the MUC channel, at least when we can identify the full JID of the
   * sender from their nick.
   *
   * Subclasses can override it to process them.
   */
  virtual void
  HandleMessage (const gloox::JID& sender, const gloox::Stanza& msg)
  {}

  /**
   * Handler called when a private message is received.
   */
  virtual void
  HandlePrivate (const gloox::JID& sender, const gloox::Stanza& msg)
  {}

  /**
   * Handler called when a participant leaves the room.  This can be used
   * to then e.g. immediately remove their orders from the orderbook.  It is
   * called with the full JID (not the nickname).
   */
  virtual void
  HandleDisconnect (const gloox::JID& disconnected)
  {}

public:

  /**
   * Sets up the client with given data, but does not yet actually
   * attempt to connect.
   */
  explicit MucClient (const gloox::JID& j, const std::string& password,
                      const gloox::JID& rm);

  virtual ~MucClient ();

  /**
   * Sets the trusted root certificate for the XMPP server connection.
   */
  void SetRootCA (const std::string& path);

  /**
   * Tries to connect to the XMPP server and join the room.  Returns true
   * on success, and false if either the connection or joining the room
   * failed.
   */
  bool Connect ();

  /**
   * Closes the connection.
   */
  void Disconnect ();

  /**
   * Returns true if the client is connected properly.
   */
  bool
  IsConnected () const
  {
    return !disconnecting && XmppClient::IsConnected ();
  }

  /**
   * Registers a given stanza extension with the underlying client.
   */
  void RegisterExtension (std::unique_ptr<gloox::StanzaExtension> ext);

  /**
   * Publishes a message to the channel.  The actual gloox message is
   * constructed internally with the right type and "to", and will carry
   * all the given stanza extensions (of which ownership is taken).
   */
  void PublishMessage (ExtensionData&& ext);

  /**
   * Sends a private message to a target JID.  Note that Democrit uses "real"
   * XMPP messages to the actual JID for private messaging, not MUC private
   * messages.
   */
  void SendMessage (const gloox::JID& to, ExtensionData&& ext);

};

} // namespace democrit

#endif // DEMOCRIT_MUCCLIENT_HPP
