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

#ifndef DEMOCRIT_STANZAS_HPP
#define DEMOCRIT_STANZAS_HPP

#include "proto/orders.pb.h"

#include <gloox/stanzaextension.h>
#include <gloox/tag.h>

#include <string>

namespace democrit
{

/**
 * StanzaExtension that encodes a specific protocol buffer type into
 * its data using Charon's XmlPayload functionality.
 */
template <typename Proto, int N, typename Self>
  class ProtoStanza : public gloox::StanzaExtension
{

private:

  /** The underlying protocol buffer data.  */
  Proto data;

  /** Set to false if this is invalid (e.g. failed to parse).  */
  bool valid;

public:

  /** The underlying type of protocol buffer.  */
  using ProtoType = Proto;

  /** XML namespace for our Democrit stanza tags.  */
  static constexpr const char* XMLNS = "https://xaya.io/democrit/";

  /** Extension type for this stanza.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + N;

  /**
   * Constructs an empty instance with default proto data, e.g. to use
   * as a factory.
   */
  ProtoStanza ();

  /**
   * Constructs an instance with the given underlying data.
   */
  explicit ProtoStanza (const Proto& d);

  /**
   * Constructs an instance from a given tag.
   */
  explicit ProtoStanza (const gloox::Tag& t);

  bool
  IsValid () const
  {
    return valid;
  }

  const Proto&
  GetData () const
  {
    return data;
  }

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * Stanza for encoding orders of an account, as sent by the user to
 * the broadcast channel.
 */
class AccountOrdersStanza
    : public ProtoStanza<proto::OrdersOfAccount, 1, AccountOrdersStanza>
{

public:

  static constexpr const char* TAG = "orders";

  using ProtoStanza::ProtoStanza;

};

} // namespace democrit

#include "../stanzas.tpp"

#endif // DEMOCRIT_STANZAS_HPP
