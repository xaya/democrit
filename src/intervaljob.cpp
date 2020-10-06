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

#include "private/intervaljob.hpp"

namespace democrit
{

IntervalJob::~IntervalJob ()
{
  {
    std::lock_guard<std::mutex> lock(mut);
    stop = true;
    cv.notify_all ();
  }

  worker.join ();
}

void
IntervalJob::StartWorker ()
{
  std::lock_guard<std::mutex> lock(mut);
  stop = false;

  worker = std::thread ([this] ()
    {
      std::unique_lock<std::mutex> lock(mut);
      while (!stop)
        {
          job ();
          cv.wait_for (lock, intv);
        }
    });
}

} // namespace democrit
