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

"""
Python wrappers for running Democrit daemons for integration testing.
"""

import copy
import jsonrpclib
import logging
import os
import subprocess
import time


# Order timeout (in seconds) that we use for tests.  It is deliberately short
# so we can easily test timeout behaviour.
ORDER_TIMEOUT = 0.1

# Trade timeout (also the trade refresh interval) used in tests.
TRADE_TIMEOUT = 0.1


class Daemon:
  """
  A context manager that runs a Democrit daemon (assumed to have the standard
  RPC interface) while the context is active.  It tries to clean the process
  up afterwards.
  """

  def __init__ (self, basedir, binary, port, gspRpcUrl, xayaRpcUrl, demGspUrl,
                account, jid, password, room, extraArgs=[]):
    """
    Constructs the manager, which will run the Democrit binary located
    at the given path, setting its log directory and JSON-RPC port as provided.
    """

    self.log = logging.getLogger ("democrit")

    self.args = [binary]
    self.args.extend (["--rpc_port", str (port)])
    self.args.extend (["--gsp_rpc_url", gspRpcUrl])
    self.args.extend (["--xaya_rpc_url", xayaRpcUrl])
    self.args.extend (["--dem_rpc_url", demGspUrl])
    self.args.extend (["--account", account])
    self.args.extend (["--jid", jid])
    self.args.extend (["--password", password])
    self.args.extend (["--room", room])
    self.args.extend (["--democrit_xid_servers", "localhost"])
    self.args.extend (["--democrit_order_timeout_ms",
                       str (int (ORDER_TIMEOUT * 1_000))])
    self.args.extend (["--democrit_confirmations", str (10)])
    self.args.extend (["--democrit_trade_timeout_ms",
                       str (int (TRADE_TIMEOUT * 1_000))])
    self.args.extend (extraArgs)

    self.cafile = None

    self.account = account
    self.basedir = basedir
    self.rpcurl = "http://localhost:%d" % port
    self.proc = None

  def __enter__ (self):
    assert self.proc is None

    self.log.info ("Starting new Democrit daemon for %s..." % self.account)

    envVars = dict (os.environ)
    envVars["GLOG_log_dir"] = self.basedir

    args = copy.deepcopy (self.args)
    if self.cafile is not None:
      args.extend (["--cafile", self.cafile])

    self.proc = subprocess.Popen (args, env=envVars)
    self.rpc = self.createRpc ()

    while True:
      try:
        state = self.rpc.getstatus ()
        if state["connected"]:
          self.log.info ("Democrit daemon for %s is connected" % self.account)
          break
      except:
        time.sleep (0.01)

    return self

  def __exit__ (self, exc, value, traceback):
    assert self.proc is not None

    self.log.info ("Stopping Democrit daemon for %s..." % self.account)
    self.proc.terminate ()
    self.proc.wait ()
    self.proc = None

  def createRpc (self):
    """
    Returns a fresh JSON-RPC client connection to the process' local server.
    """

    return jsonrpclib.ServerProxy (self.rpcurl)

  def addOrder (self, typ, asset, price, maxUnits, minUnits=None):
    """
    Sends an addorder RPC to the Democrit daemon, constructing the order
    JSON from the arguments.
    """

    order = {
      "asset": asset,
      "type": typ,
      "price_sat": int (1e8 * price),
      "max_units": maxUnits,
    }
    if minUnits is not None:
      order["min_units"] = minUnits

    return self.rpc.addorder (order=order)

  def getTrades (self):
    """
    Returns the list of trades as per the gettrades RPC method.  It strips
    out the start_time fields, since those cannot be directly compared to
    golden data.
    """

    data = self.rpc.gettrades ()

    for d in data:
      assert "start_time" in d
      del d["start_time"]

    return data
