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

#ifndef DEMOCRIT_STATE_HPP
#define DEMOCRIT_STATE_HPP

#include "proto/state.pb.h"

#include <mutex>

namespace democrit
{

/**
 * Wrapper around the global state that an instance holds in form of
 * a State proto.  It mostly handles synchronisation for accessing the state.
 */
class State
{

private:

  /** The actual data instance.  */
  proto::State state;

  /**
   * Mutex for the state.  This could be a std::shared_mutex for read/write
   * locking, but for now should be sufficient with exlusive locks only
   * until we use C++17.
   */
  mutable std::mutex mut;

public:

  State () = default;

  State (const State&) = delete;
  void operator= (const State&) = delete;

  /**
   * Exposes the state in a mutable form within the callback.
   */
  template <typename Fcn>
    void
    AccessState (const Fcn& f)
  {
    std::lock_guard<std::mutex> lock(mut);
    f (state);
  }

  /**
   * Exposes the state in a read-only form within the callback.
   */
  template <typename Fcn>
    void
    ReadState (const Fcn& f) const
  {
    std::lock_guard<std::mutex> lock(mut);
    f (state);
  }

};

} // namespace democrit

#endif // DEMOCRIT_STATE_HPP
