#!/usr/bin/env python

#   Democrit - atomic trades for XAYA games
#   Copyright (C) 2020  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Basic test for the Democrit GSP."""

from xayagametest.testcase import XayaGameTest

import os
import os.path
import time


class DemGspTest (XayaGameTest):

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    binary = os.path.join (top_builddir, "gsp", "democrit-gsp")
    super (DemGspTest, self).__init__ ("dem", binary)

  def expectPending (self, expected):
    time.sleep (0.05)
    self.assertEqual (self.getPendingState (), expected)

  def run (self):
    self.collectPremine ()
    # For some reason we have to split the single premine coin into
    # multiple ones, otherwise the wallet does not like the reorg test.
    sendTo = {
      self.rpc.xaya.getnewaddress (): 100
      for _ in range (5)
    }
    self.rpc.xaya.sendmany ("", sendTo)
    self.generate (1)
    self.expectGameState ({})
    self.expectPending ({})

    self.mainLogger.info ("Sending some basic moves...")
    self.sendMove ("foo", 42)
    self.sendMove ("bar", {})
    self.sendMove ("baz", {"t": 42})
    self.sendMove ("foo", {"t": "c"})
    self.sendMove ("baz", {"t": "b", "x": "ignored"})
    self.expectPending ({
      "foo": ["c"],
      "baz": ["b"],
    })
    self.generate (1)
    self.expectPending ({})
    self.sendMove ("foo", {"t": "a"})
    self.expectPending ({
      "foo": ["a"],
    })
    self.generate (1)
    self.expectGameState ({
      "foo": ["a", "c"],
      "baz": ["b"],
    })
    reorgBlk = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Duplicate IDs are just ignored...")
    self.sendMove ("foo", {"t": "c"})
    self.sendMove ("foo", {"t": "c"})
    self.sendMove ("baz", {"t": "b"})
    self.expectPending ({
      "foo": ["c"],
      "baz": ["b"],
    })
    self.generate (1)
    self.expectGameState ({
      "foo": ["a", "c"],
      "baz": ["b"],
    })

    self.mainLogger.info ("Same ID with another name...")
    self.sendMove ("bar", {"t": "a"})
    self.sendMove ("bar", {"t": "b"})
    self.sendMove ("bar", {"t": "c"})
    self.sendMove ("foo", {"t": "b"})
    self.expectPending ({
      "foo": ["b"],
      "bar": ["a", "b", "c"],
    })
    self.generate (1)
    self.expectGameState ({
      "foo": ["a", "b", "c"],
      "bar": ["a", "b", "c"],
      "baz": ["b"],
    })

    self.mainLogger.info ("Testing reorg...")
    self.generate (20)
    oldState = self.getGameState ()
    self.rpc.xaya.invalidateblock (reorgBlk)

    self.expectGameState ({
      "foo": ["c"],
      "baz": ["b"],
    })
    self.sendMove ("foo", {"t": "x"})
    self.generate (1)
    self.sendMove ("baz", {"t": "y"})
    self.expectPending ({
      "baz": ["y"],
    })
    self.expectGameState ({
      "foo": ["c", "x"],
      "baz": ["b"],
    })

    self.rpc.xaya.reconsiderblock (reorgBlk)
    self.expectGameState (oldState)
    self.expectPending ({})


if __name__ == "__main__":
  DemGspTest ().main ()
