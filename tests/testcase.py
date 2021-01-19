#   Democrit - atomic trades for XAYA games
#   Copyright (C) 2020-2021  Autonomous Worlds Ltd
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

from xayagametest import game
from xayagametest.testcase import XayaGameTest

import democrit

from contextlib import contextmanager
import os
import os.path
import shutil
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

  def setup (self):
    self.log.info ("Starting Democrit GSP...")

    binary = self.getBinaryPath ("gsp", "democrit-gsp")

    base = os.path.join (self.basedir, "dem")
    shutil.rmtree (base, ignore_errors=True)
    os.mkdir (base)

    self.demGsp = game.Node (base, self.basePort + 10, [binary])
    self.demGsp.start (self.xayanode.rpcurl, wait=True)

  def shutdown (self):
    self.demGsp.stop ()

  def syncGame (self):
    """
    Makes sure both the underlying nonfungible and Democrit GSPs are synced
    up-to-date with Xaya Core's best block.
    """

    super ().syncGame ()

    bestBlk = self.rpc.xaya.getbestblockhash ()
    while True:
      state = self.demGsp.rpc.getnullstate ()
      self.assertEqual (state["gameid"], "dem")
      self.assertEqual (state["chain"], "regtest")
      if state["state"] == "up-to-date" and state["blockhash"] == bestBlk:
        return
      time.sleep (0.01)

  @contextmanager
  def runDemocrit (self, wallet=""):
    """
    Returns a context manager that runs a Democrit daemon with one of our
    test accounts.

    Optionally, the Democrit daemon can be connected to a non-default
    wallet in Xaya Core, so that e.g. two wallets (but within the same
    Xaya Core) can trade with each other.
    """

    binary = self.getBinaryPath ("nonfungible", "nonfungible-democrit")

    accountIndex = None
    for i in range (len (self.accountInUse)):
      if not self.accountInUse[i]:
        accountIndex = i
        break
    if accountIndex is None:
      raise RuntimeError ("no free account for another Democrit daemon")

    # This will be set to a JSON-RPC ServerProxy for the Xaya Core
    # with our selected wallet.  We need to make sure to clean it up
    # after the test (close the socket), or else Xaya Core will wait
    # for the connection to be closed on shutdown.
    xaya = None

    try:
      self.accountInUse[accountIndex] = True
      port = self.basePort + 11 + accountIndex

      accountConfig = XMPP_CONFIG["accounts"][accountIndex]
      account = accountConfig[0]
      basedir = os.path.join (self.basedir, "democrit", account)
      os.makedirs (basedir, exist_ok=True)
      jid = "%s@%s" % (accountConfig[0], XMPP_CONFIG["server"])

      xayaUrl, xaya = self.xayanode.getWalletRpc (wallet)

      with democrit.Daemon (basedir, binary, port, self.gamenode.rpcurl,
                            xayaUrl, self.demGsp.rpcurl,
                            account, jid, accountConfig[1],
                            XMPP_CONFIG["room"]) as d:
        # For convenience (e.g. to send CHI to the daemon's wallet), we store
        # an RPC handle for the selected wallet inside the instance.
        d.xaya = xaya
        yield d
    finally:
      self.accountInUse[accountIndex] = False
      if xaya:
        xaya ("close") ()

  def sleepSome (self):
    """
    Sleeps a short amount of time, which should be enough in tests to
    let all daemons sync through XMPP.
    """

    time.sleep (0.01)

  def updateTrades (self):
    """
    Sleeps a long enough time to make sure the trade update has been run
    (e.g. timing out abandoned ones) by all active daemons.
    """

    self.syncGame ()
    time.sleep (1.5 * democrit.TRADE_TIMEOUT)

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

  def getBinaryPath (self, *components):
    """
    Returns the path to a binary that is somewhere in our build folder.
    """

    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."

    return os.path.join (top_builddir, *components)

  def expectApproxBalance (self, xaya, expected):
    """
    Expects that the balance of the given Xaya RPC connection is
    approximately (+- some for fees) the expected amount.
    """

    eps = 0.1
    actual = xaya.getbalance ()
    assert actual >= expected - eps
    assert actual <= expected + eps

  def expectAsset (self, account, asset, expected):
    """
    Expects that the balance of the given asset with the given account
    (in the nonfungible game state) matches some value.
    """

    actual = self.getCustomState ("data", "getbalance",
                                  name=account, asset=asset)
    self.assertEqual (actual, expected)
