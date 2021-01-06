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

#ifndef DEMOCRIT_MOCKXAYA_HPP
#define DEMOCRIT_MOCKXAYA_HPP

#include "private/rpcclient.hpp"
#include "proto/trades.pb.h"
#include "rpc-stubs/xayarpcclient.h"
#include "rpc-stubs/xayarpcserverstub.h"
#include "testutils.hpp"

#include <xayautil/uint256.hpp>

#include <json/json.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gmock/gmock.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace democrit
{

/**
 * Utility method for generating server ports to be used.  It uses an
 * internal call counter to cycle through some range, which should be good
 * enough to find free ports even if more than one mock server are running
 * at the same time.
 */
int GetPortForMockServer ();

/**
 * Mock Xaya RPC server.  It implements the RPC methods needed to process
 * Democrit trades, but uses hard-coded data or simple fake logic for most
 * of the things.
 */
class MockXayaRpcServer : public XayaRpcServerStub
{

private:

  /** How many addresses have been created already.  */
  unsigned addrCount = 0;

  /**
   * UTXO entries that have been added explicitly with AddUtxo.  They are
   * returned by the gettxout method.
   */
  std::set<std::pair<std::string, int>> utxos;

  /**
   * Decoded JSON values to be returned for PSBTs from decodepsbt.  The keys
   * here (the PSBT strings) are just arbitrary, and do not correspond to
   * an actual PSBT format.
   */
  std::map<std::string, Json::Value> psbts;

  /** The current best block, e.g. returned as part of gettxout.  */
  xaya::uint256 bestBlock;

public:

  explicit MockXayaRpcServer (jsonrpc::AbstractServerConnector& conn);

  /**
   * Sets the best block to be returned by methods like gettxout.
   */
  void
  SetBestBlock (const xaya::uint256& b)
  {
    bestBlock = b;
  }

  /**
   * Adds an UTXO entry as "known", which will be returned by gettxout.
   */
  void
  AddUtxo (const std::string& txid, const int vout)
  {
    utxos.emplace (txid, vout);
  }

  /**
   * Sets the JSON value that should be returned as "decoded" form
   * of a given PSBT.  The psbt string itself is just used as lookup key,
   * and does not correspond to a real PSBT format.
   */
  void
  SetPsbt (const std::string& psbt, const Json::Value& decoded)
  {
    psbts[psbt] = decoded;
  }

  /**
   * Sets up the call expectations for joinpsbts, joining the given two PSBTs.
   * This actually assumes they are known (from SetPsbt), and combines the
   * joined PSBT value internally, setting it for the given combined PSBT
   * identifier string.
   */
  void
  SetJoinedPsbt (const std::vector<std::string>& psbtsIn,
                 const std::string& combined);

  /**
   * Sets up the call expectations necessary to successfully allow a buyer
   * to construct the unsigned trade PSBT.
   *
   * The seller's name is given, and its initial outpoint (i.e. the name input)
   * is set to "nm txid:vout".  The seller's addresses are taken from the
   * seller data, and the total sent to the seller in CHI satoshi is given as
   * well.
   *
   * For the buyer, we add two CHI inputs, "buyer txid:1" and "buyer txid:2".
   * We add one change output to "change addr" and with a dummy change value.
   *
   * The final constructed unsigned PSBT will be returned by
   * Trade::ConstructTransaction with the given identifier string.
   */
  void PrepareConstructTransaction (const std::string& psbt,
                                    const std::string& seller,
                                    int vout, const proto::SellerData& sd,
                                    Amount total, const std::string& move);

  /**
   * Sets up the expectations for a call to walletprocesspsbt with the
   * given input PSBT, returning a defined PSBT identifier for the "signed"
   * transaction.  This also sets up a decoded form for the "signed" PSBT, which
   * marks all inputs matching the input txids given as signed.  The call to
   * walletprocesspsbt will return "complete" if all inputs are marked
   * as signed afterwards (based on this and previous calls to SetSignedPsbt).
   */
  void SetSignedPsbt (const std::string& signedPsbt,
                      const std::string& psbt,
                      const std::set<std::string>& signTxids);

  /**
   * Returns the block hash that the mock server "has" at some height
   * (e.g. with getblockheader and its prev hashes).
   */
  static xaya::uint256 GetBlockHash (unsigned height);

  /**
   * The addresses returned are of the form "addr N", which N counting
   * how many have been created already.
   */
  std::string getnewaddress () override;

  /**
   * The name "p/invalid" is assumed to not exist and will throw.  For other
   * names starting with "p/", e.g. "p/nm", the method will return the outpoint
   * "nm txid:12" with the name filled into the txhash.
   */
  Json::Value name_show (const std::string& name) override;

  /**
   * If the queried UTXO has been added with AddUtxo, then this method
   * returns it together with the currently set best block hash.  Otherwise
   * it returns null, like Xaya Core's gettxout.
   */
  Json::Value gettxout (const std::string& txid, int vout) override;

  /**
   * The server has a static list of block hashes corresponding to fixed heights
   * (as per GetBlockHash).  This method checks if the given hash is one
   * of the first couple of them; if it is, the method returns a base result
   * with just the "previousblockhash" set.  If it is not in there, then the
   * method throws.
   */
  Json::Value getblockheader (const std::string& hashStr) override;

  /**
   * Returns the previously set JSON value (with SetPsbt) for the given
   * psbt value (which is just used as lookup key).  If the string does not
   * correspond to a known PSBT, throws as if it were invalid.
   */
  Json::Value decodepsbt (const std::string& psbt) override;

  /**
   * This method is a wrapper around the gmock'ed CreateFundedPsbt, which
   * takes the raw-PSBT result and puts it into a JSON object as returned
   * by the actual Xaya RPC method.  It also asserts that the lockTime passed
   * in is always zero.
   */
  Json::Value walletcreatefundedpsbt (const Json::Value& inputs,
                                      const Json::Value& outputs,
                                      int lockTime,
                                      const Json::Value& options) override;

  /**
   * Wraps the mocked NamePsbt method.  This method expects the name_op
   * argument to be for a name-update with some value, and passes the
   * updated name/value on to NamePsbt.  It also takes the string result
   * of the wrapped method and returns it inside a JSON object as per the
   * namepsbt RPC method.
   */
  Json::Value namepsbt (const std::string& psbt, int vout,
                        const Json::Value& nameOp) override;

  /**
   * Combines signatures in the given PSBTs.  It expects that we have a decoded
   * form for all of them; based on that, it will produce a combined
   * decoded JSON (putting together "signatures" as per SetSignedPsbt)
   * and store it as the decoded form of "psbt 1 + psbt 2 + ..." for inputs
   * "psbt 1", "psbt 2" and so on.
   */
  std::string combinepsbt (const Json::Value& inputPsbts) override;

  /**
   * Tries to finalise a PSBT.  It assumes that we know the decoded form
   * of it.  The method then checks if all "inputs" have "signed":true set;
   * if so, it returns a fake "complete" result with the "hex" field
   * set to "rawtx <psbt>".  If not, then it returns "complete":false
   * and the input PSBT.
   */
  Json::Value finalizepsbt (const std::string& psbt) override;

  /* The following methods are just mocked using gmock and should be used
     with explicit call expectations.  */
  MOCK_METHOD3 (CreateFundedPsbt,
                std::string (const Json::Value& inputs,
                             const Json::Value& outputs,
                             const Json::Value& options));
  MOCK_METHOD2 (createpsbt,
                std::string (const Json::Value& inputs,
                             const Json::Value& outputs));
  MOCK_METHOD4 (NamePsbt,
                std::string (const std::string& psbt, int vout,
                             const std::string& name,
                             const std::string& value));
  MOCK_METHOD1 (joinpsbts, std::string (const Json::Value& psbts));
  MOCK_METHOD1 (walletprocesspsbt, Json::Value (const std::string& psbt));
  MOCK_METHOD1 (sendrawtransaction, std::string (const std::string& hex));

};

/**
 * Test environment with a mock Xaya RPC server (that can be parametrised
 * using the template parameter).  It starts a real HTTP server with the
 * mock RPC as backend, and sets up an RPC client that tests can use.
 */
template <typename XayaServer>
  class TestEnvironment
{

private:

  /** Asset spec for the test.  */
  TestAssets assets;

  /** Port for the Xaya RPC server.  */
  const int xayaPort;

  /** HTTP server used for the test.  */
  jsonrpc::HttpServer xayaHttpServer;

  /** The Xaya RPC server.  */
  XayaServer xayaRpcServer;

  /** The RPC client connected to our mock Xaya server.  */
  RpcClient<XayaRpcClient> xayaClient;

  /**
   * Returns the HTTP endpoint for the Xaya test server at the given port.
   */
  static std::string GetXayaEndpoint (int port);

public:

  TestEnvironment ();
  ~TestEnvironment ();

  /**
   * Exposes the mock Xaya RPC server, e.g. for controlling it from a test.
   */
  XayaServer&
  GetXayaServer ()
  {
    return xayaRpcServer;
  }

  /**
   * Exposes the Xaya RPC client for tests.
   */
  RpcClient<XayaRpcClient>&
  GetXayaRpc ()
  {
    return xayaClient;
  }

  /**
   * Returns the asset spec to use.
   */
  TestAssets&
  GetAssetSpec ()
  {
    return assets;
  }

};

} // namespace democrit

#include "mockxaya.tpp"

#endif // DEMOCRIT_MOCKXAYA_HPP
