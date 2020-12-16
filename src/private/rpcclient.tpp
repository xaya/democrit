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

/* Template implementation code for rpcclient.hpp.  */

namespace democrit
{

template <typename T>
  T&
  RpcClient<T>::operator* ()
{
  std::lock_guard<std::mutex> lock(mut);
  const auto id = std::this_thread::get_id ();

  const auto mit = rpcClients.find (id);
  if (mit != rpcClients.end ())
    return mit->second;

  const auto http = httpClients.emplace (id, endpoint);
  const auto rpc = rpcClients.emplace (id, http.first->second);

  return rpc.first->second;
}

} // namespace democrit
