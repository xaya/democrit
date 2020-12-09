#!/usr/bin/env python3

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

  def expectState (self, btxid, state):
    time.sleep (0.05)
    self.assertEqual (self.rpc.game.checktrade (btxid), state)

  def sendMove (self, name, mv={}):
    """
    Sends a move with the given name for our game.  The difference to the
    normal method (per the superclass) is that it returns the btxid rather
    than txid, since that is what we need later to query the game state.
    """

    txid = super ().sendMove (name, mv)
    data = self.rpc.xaya.getrawtransaction (txid, True)
    return data["btxid"]

  def run (self):
    self.collectPremine ()
    # For some reason we have to split the single premine coin into
    # multiple ones, otherwise the wallet does not like the reorg test.
    sendTo = {
      self.rpc.xaya.getnewaddress (): 100
      for _ in range (5)
    }
    self.rpc.xaya.sendmany ("", sendTo)
    self.generate (2)
    self.expectGameState ({})
    self.expectPending ({})
    unknownHash = "aa" * 32
    self.expectState (unknownHash, {"state": "unknown"})
    reorgBlk = self.rpc.xaya.getbestblockhash ()

    self.mainLogger.info ("Sending some moves...")
    id1 = self.sendMove ("foo")
    id2 = self.sendMove ("bar", mv="42")
    id3 = self.sendMove ("foo")
    self.expectPending ({
      id1: {},
      id2: {},
      id3: {},
    })
    self.expectState (id1, {"state": "pending"})
    self.generate (1)
    height = self.rpc.xaya.getblockcount ()
    self.generate (20)
    self.expectPending ({})
    self.expectGameState ({
      id1: height,
      id2: height,
      id3: height,
    })
    self.expectState (id2, {"state": "confirmed", "height": height})

    self.mainLogger.info ("Testing reorg...")
    oldState = self.getGameState ()
    self.rpc.xaya.invalidateblock (reorgBlk)

    self.expectPending ({})
    self.expectGameState ({})
    self.expectState (id1, {"state": "unknown"})

    self.generate (2)
    idReorg = self.sendMove ("foo")
    self.generate (1)
    self.expectGameState ({
      idReorg: height + 1,
    })
    self.expectState (id1, {"state": "unknown"})
    self.expectState (idReorg, {"state": "confirmed", "height": height + 1})

    self.rpc.xaya.reconsiderblock (reorgBlk)
    self.expectGameState (oldState)
    self.expectState (idReorg, {"state": "unknown"})


if __name__ == "__main__":
  DemGspTest ().main ()
