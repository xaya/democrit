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

#ifndef DEMOCRIT_INTERVALJOB_HPP
#define DEMOCRIT_INTERVALJOB_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace democrit
{

/**
 * A generic thread that runs a given job at set intervals until it is
 * destructed.  This is used for things like broadcasting our own
 * orders and timing out other orders.
 *
 * The interval is not exactly guaranteed, but the job will be run approximately
 * with that frequency (it might be a bit earlier or later depending on
 * circumstances).
 */
class IntervalJob
{

private:

  /** The interval for repeating the job.  */
  const std::chrono::nanoseconds intv;

  /** The job to execute.  */
  std::function<void ()> job;

  /** Mutex for this instance and its condition variable.  */
  std::mutex mut;

  /** Set to true to signal that the worker thread should stop.  */
  bool stop;

  /**
   * Used to signal the worker thread when it should stop, so that we can wake
   * it up immediately without having to wait for a full interval sleep.
   */
  std::condition_variable cv;

  /** The worker thread.  */
  std::thread worker;

  /**
   * Starts the worker thread.
   */
  void StartWorker ();

public:

  /**
   * Constructs the job, which starts the worker immediately.
   */
  template <typename Fcn, typename Rep, typename Period>
    explicit IntervalJob (const std::chrono::duration<Rep, Period> i,
                          const Fcn& j)
    : intv(i), job(j)
  {
    StartWorker ();
  }

  /**
   * Destroys the job, which stops the worker.
   */
  ~IntervalJob ();

  IntervalJob () = delete;
  IntervalJob (const IntervalJob&) = delete;
  void operator= (const IntervalJob&) = delete;

};

} // namespace democrit

#endif // DEMOCRIT_INTERVALJOB_HPP
