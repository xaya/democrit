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

from xayagametest.testcase import XayaGameTest

import democrit

from contextlib import contextmanager
import os
import os.path
import time


# XMPP configuration for our test environment (using the Charon
# test XMPP Docker image).
XMPP_CONFIG = {
  "server": "localhost",
  "room": "democrit-nf@muc.localhost",
  "accounts": [
    ("xmpptest1", "password"),
    ("xmpptest2", "password"),
    ("xmpptest3", "password"),
  ],
}


class NonFungibleTest (XayaGameTest):
  """
  Integration test that runs the non-fungible GSP on regtest
  and supports running Democrit daemons for it.
  """

  def __init__ (self):
    super ().__init__ ("nf", "nonfungibled")

    # For each of the XMPP accounts we have, we keep track if it is
    # being used as part of an active Democrit daemon at the moment,
    # so that we can give out free ports and accounts as needed for
    # new daemons.
    self.accountInUse = [False] * len (XMPP_CONFIG["accounts"])

  @contextmanager
  def runDemocrit (self):
    """
    Returns a context manager that runs a Democrit daemon with one of our
    test accounts.
    """

    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    binary = os.path.join (top_builddir, "nonfungible", "nonfungible-democrit")

    accountIndex = None
    for i in range (len (self.accountInUse)):
      if not self.accountInUse[i]:
        accountIndex = i
        break
    if accountIndex is None:
      raise RuntimeError ("no free account for another Democrit daemon")

    try:
      self.accountInUse[accountIndex] = True
      port = self.basePort + 5 + accountIndex

      accountConfig = XMPP_CONFIG["accounts"][accountIndex]
      account = accountConfig[0]
      basedir = os.path.join (self.basedir, "democrit", account)
      os.makedirs (basedir, exist_ok=True)
      jid = "%s@%s" % (accountConfig[0], XMPP_CONFIG["server"])

      with democrit.Daemon (basedir, binary, port, self.gamenode.rpcurl,
                            account, jid, accountConfig[1],
                            XMPP_CONFIG["room"]) as d:
        yield d
    finally:
      self.accountInUse[accountIndex] = False

  def sleepSome (self):
    """
    Sleeps a short amount of time, which should be enough in tests to
    let all daemons sync through XMPP.
    """

    time.sleep (0.01)

  def jsonAsset (self, minter, asset):
    """
    Returns the JSON form of a non-fungible asset.
    """

    return {"m": minter, "a": asset}

  def democritAsset (self, minter, asset):
    """
    Returns the Democrit asset string for a non-fungible asset.
    """

    return "%s\n%s" % (minter, asset)
