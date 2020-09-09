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

/* Template implementation code for stanzas.hpp.  */

#include <charon/xmldata.hpp>

#include <glog/logging.h>

#include <memory>

namespace democrit
{

template <typename Proto, int N, typename Self>
  constexpr const char* ProtoStanza<Proto, N, Self>::XMLNS;

template <typename Proto, int N, typename Self>
  ProtoStanza<Proto, N, Self>::ProtoStanza ()
  : StanzaExtension(EXT_TYPE), valid(false)
{}

template <typename Proto, int N, typename Self>
  ProtoStanza<Proto, N, Self>::ProtoStanza (const Proto& d)
  : StanzaExtension(EXT_TYPE), data(d), valid(true)
{}

template <typename Proto, int N, typename Self>
  ProtoStanza<Proto, N, Self>::ProtoStanza (const gloox::Tag& t)
  : StanzaExtension(EXT_TYPE), valid(false)
{
  /* We start with valid=false and only set it to true if we have successfully
     done all our parsing.  */

  std::string payload;
  if (!charon::DecodeXmlPayload (t, payload))
    return;

  if (!data.ParseFromString (payload))
    return;

  valid = true;
}

template <typename Proto, int N, typename Self>
  const std::string&
  ProtoStanza<Proto, N, Self>::filterString () const
{
  static const std::string filter
      = "/*/" + std::string (Self::TAG) + "[@xmlns='" + XMLNS + "']";
  return filter;
}

template <typename Proto, int N, typename Self>
  gloox::StanzaExtension*
  ProtoStanza<Proto, N, Self>::newInstance (const gloox::Tag* t) const
{
  return new Self (*t);
}

template <typename Proto, int N, typename Self>
  gloox::StanzaExtension*
  ProtoStanza<Proto, N, Self>::clone () const
{
  auto res = std::make_unique<Self> ();
  res->data = data;
  res->valid = valid;
  return res.release ();
}

template <typename Proto, int N, typename Self>
  gloox::Tag*
  ProtoStanza<Proto, N, Self>::tag () const
{
  CHECK (IsValid ()) << "Trying to serialise an invalid stanza";

  std::string payload;
  CHECK (data.SerializeToString (&payload));

  auto res = charon::EncodeXmlPayload (Self::TAG, payload);
  res->setXmlns (XMLNS);

  return res.release ();
}

} // namespace democrit
