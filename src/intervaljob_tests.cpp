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

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace democrit
{
namespace
{

class IntervalJobTests : public testing::Test
{

private:

  /** Counter that is incremented for each job execution.  */
  std::atomic<unsigned> counter;

protected:

  /** The interval we use in our tests.  */
  static constexpr auto INTV = std::chrono::milliseconds (10);

  IntervalJobTests ()
  {
    counter = 0;
  }

  /**
   * Starts an interval job and returns the instance.  The executed
   * job increments our counter.
   */
  std::unique_ptr<IntervalJob>
  StartJob (const std::chrono::milliseconds intv = INTV)
  {
    return std::make_unique<IntervalJob> (intv, [this] ()
      {
        ++counter;
      });
  }

  /**
   * Expects a given counter value.
   */
  void
  ExpectCount (const unsigned expected) const
  {
    EXPECT_EQ (counter, expected);
  }

};

TEST_F (IntervalJobTests, JobExecuted)
{
  auto job = StartJob ();
  std::this_thread::sleep_for (3.5 * INTV);
  job.reset ();

  ExpectCount (4);
}

TEST_F (IntervalJobTests, QuickShutdown)
{
  using Clock = std::chrono::steady_clock;

  const auto before = Clock::now ();
  auto job = StartJob (10 * INTV);
  std::this_thread::sleep_for (INTV);
  job.reset ();
  const auto after = Clock::now ();

  ExpectCount (1);
  EXPECT_LT (after - before, 2 * INTV);
}

} // anonymous namespace
} // namespace democrit
