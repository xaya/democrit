/*
    Democrit - atomic trades for XAYA games
    Copyright (C) 2020-2021  Autonomous Worlds Ltd

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

#include "private/trades.hpp"

#include <xayautil/jsonutils.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <sstream>

namespace democrit
{

DEFINE_int32 (democrit_confirmations, 6,
              "Block confirmations until a trade is finalised");
DEFINE_int32 (democrit_feerate_wo_names, 1'000,
              "Fee rate (in sat/vb) to use for the trade transaction"
              " without name input/output");
DEFINE_int32 (democrit_trade_timeout_ms, 30'000,
              "Milliseconds until an initiated trade will be abandoned if not"
              " finalised with the counterparty");

namespace
{

/** Value paid into name outputs (in satoshis).  */
constexpr Amount NAME_VALUE = 1'000'000;

/**
 * Tries to lock or unlock an unspent output in the Xaya Wallet.  Returns
 * true on success and false on failure.
 */
bool
LockUnspent (RpcClient<XayaRpcClient>& rpc, const bool lock,
             const proto::OutPoint& out)
{
  /* The lockunspent RPC method always returns either true or throws on
     failure, which we want to catch and translate to a return value here.  */
  try
    {
      Json::Value jsonOut(Json::objectValue);
      jsonOut["txid"] = out.hash ();
      jsonOut["vout"] = static_cast<Json::Int> (out.n ());

      Json::Value outputs(Json::arrayValue);
      outputs.append (jsonOut);

      return rpc->lockunspent (!lock, outputs);
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      VLOG (1) << "Error in lockunspent: " << exc.what ();
      return false;
    }
}

/**
 * Unlocks all inputs in the given PSBT.
 */
void
UnlockPsbtInputs (RpcClient<XayaRpcClient>& rpc, const std::string& psbt)
{
  const auto decoded = rpc->decodepsbt (psbt);
  CHECK (decoded.isObject ());
  const auto& tx = decoded["tx"];
  CHECK (tx.isObject ());
  const auto& vin = tx["vin"];
  CHECK (vin.isArray ());

  /* Note that not all inputs will be ours (at least the name input won't),
     but that is fine as LockUnspent gracefully handles unlock-errors.  */
  for (const auto& in : vin)
    LockUnspent (rpc, false, OutPointFromJson (in));
}

} // anonymous namespace

/* ************************************************************************** */

std::unique_ptr<TradeChecker>
Trade::BuildTradeChecker () const
{
  std::string buyer, seller;
  switch (GetOrderType ())
    {
    case proto::Order::BID:
      buyer = account;
      seller = pb.counterparty ();
      break;
    case proto::Order::ASK:
      buyer = pb.counterparty ();
      seller = account;
      break;
    default:
      LOG (FATAL) << "Unexpected order type: " << pb.order ().type ();
    }

  return std::make_unique<TradeChecker> (
      tm.spec, tm.xayaRpc,
      buyer, seller,
      pb.order ().asset (), pb.order ().price_sat (), pb.units ());
}

std::string
Trade::GetIdentifier () const
{
  /* New lines are not valid inside Xaya names, so they can act as
     separator between maker name and order ID.  */

  std::ostringstream res;
  res << pb.order ().account () << '\n' << pb.order ().id ();

  return res.str ();
}

proto::Order::Type
Trade::GetOrderType () const
{
  const auto role = GetRole ();
  if (role == proto::Trade::MAKER)
    return pb.order ().type ();
  CHECK_EQ (role, proto::Trade::TAKER) << "Unexpected role: " << role;

  switch (pb.order ().type ())
    {
    case proto::Order::BID:
      return proto::Order::ASK;
    case proto::Order::ASK:
      return proto::Order::BID;
    default:
      LOG (FATAL) << "Unexpected order type: " << pb.order ().type ();
    }
}

proto::Trade::Role
Trade::GetRole () const
{
  return pb.order ().account () == account
      ? proto::Trade::MAKER
      : proto::Trade::TAKER;
}

Trade::Clock::time_point
Trade::GetStartTime () const
{
  return Clock::time_point (std::chrono::seconds (pb.start_time ()));
}

void
Trade::InitProcessingMessage (proto::ProcessingMessage& msg) const
{
  msg.Clear ();
  msg.set_counterparty (pb.counterparty ());
  msg.set_identifier (GetIdentifier ());
}

void
Trade::SetTakingOrder (proto::ProcessingMessage& msg) const
{
  auto& to = *msg.mutable_taking_order ();
  to.set_id (pb.order ().id ());
  to.set_units (pb.units ());
}

bool
Trade::IsFinalised () const
{
  if (!pb.has_state ())
    return false;

  switch (pb.state ())
    {
    case proto::Trade::ABANDONED:
    case proto::Trade::SUCCESS:
    case proto::Trade::FAILED:
      return true;

    default:
      return false;
    }
}

proto::Trade
Trade::GetPublicInfo () const
{
  proto::Trade res;
  res.set_state (pb.state ());
  res.set_start_time (pb.start_time ());
  res.set_counterparty (pb.counterparty ());
  res.set_type (GetOrderType ());
  res.set_asset (pb.order ().asset ());
  res.set_units (pb.units ());
  res.set_price_sat (pb.order ().price_sat ());
  res.set_role (GetRole ());
  return res;
}

bool
Trade::Matches (const proto::ProcessingMessage& msg) const
{
  return msg.counterparty () == pb.counterparty ()
            && msg.identifier () == GetIdentifier ();
}

void
Trade::MergeSellerData (const proto::SellerData& sd)
{
  if (GetOrderType () == proto::Order::ASK)
    {
      LOG (WARNING) << "Buyer sent us seller data:\n" << sd.DebugString ();
      return;
    }

  if (pb.has_seller_data ())
    {
      LOG (WARNING)
          << "Seller data sent, but we have it already:"
          << "\nOurs:\n" << pb.seller_data ().DebugString ()
          << "\nSent:\n" << sd.DebugString ();
      return;
    }

  if (!sd.has_name_address () || !sd.has_chi_address ()
        || sd.has_name_output ())
    {
      LOG (WARNING) << "Invalid seller data received:\n" << sd.DebugString ();
      return;
    }

  /* The two addresses must not be the same, since otherwise createpsbt
     would fail (even though in theory it would be fine for the blockchain).  */
  if (sd.name_address () == sd.chi_address ())
    {
      LOG (WARNING)
          << "Seller's CHI and name address must not be equal:\n"
          << sd.DebugString ();
      return;
    }

  VLOG (1)
      << "Got seller data for trade " << GetIdentifier ()
      << ":\n" << sd.DebugString ();
  *pb.mutable_seller_data () = sd;
}

void
Trade::MergePsbt (const proto::TradePsbt& psbt)
{
  if (pb.has_their_psbt ())
    {
      LOG (WARNING)
          << "Received PSBT but already have the counterparty's:"
          << "\nExisting:\n" << pb.their_psbt ()
          << "\nSent:\n" << psbt.DebugString ();
      return;
    }

  pb.set_their_psbt (psbt.psbt ());
  VLOG (1) << "Got PSBT from counterparty:\n" << pb.their_psbt ();
}

void
Trade::HandleMessage (const proto::ProcessingMessage& msg)
{
  CHECK (isMutable) << "Trade instance is not mutable";

  /* In any state except INITIATED, there is nothing more to do except
     potentially wait (if the state is PENDING).  */
  if (pb.state () != proto::Trade::INITIATED)
    return;

  if (msg.has_seller_data ())
    MergeSellerData (msg.seller_data ());

  if (msg.has_psbt ())
    MergePsbt (msg.psbt ());
}

bool
Trade::CreateSellerData ()
{
  if (GetOrderType () != proto::Order::ASK)
    return false;
  if (pb.has_seller_data ())
    return false;

  proto::SellerData sd;
  *sd.mutable_name_output () = GetNameOutPoint (tm.xayaRpc, account);

  if (!LockUnspent (tm.xayaRpc, true, sd.name_output ()))
    {
      LOG (WARNING) << "Failed to lock name output for " << account;
      return false;
    }

  sd.set_name_address (tm.xayaRpc->getnewaddress ());
  sd.set_chi_address (tm.xayaRpc->getnewaddress ());

  *pb.mutable_seller_data () = std::move (sd);
  return true;
}

std::string
Trade::ConstructTransaction (const TradeChecker& checker,
                             const proto::OutPoint& nameIn) const
{
  CHECK_EQ (GetOrderType (), proto::Order::BID)
      << "The buyer should construct the transaction";
  const auto& sd = pb.seller_data ();
  CHECK (sd.has_chi_address () && sd.has_name_address ())
      << "Missing or invalid seller data:\n" << pb.DebugString ();

  VLOG (1)
      << "Constructing trade transaction for:\n" << pb.DebugString ();

  const std::string& sellerName = pb.counterparty ();
  Amount total;
  CHECK (checker.GetTotalSat (total));

  /* First step:  Let the wallet fund a transaction paying the seller
     their CHI, but without the name input or output.  This determines the
     coins spent by the buyer, and also the change they get.  */
  std::string chiPart;
  {
    Json::Value outputs(Json::arrayValue);
    Json::Value cur(Json::objectValue);
    cur[sd.chi_address ()] = xaya::ChiAmountToJson (total);
    outputs.append (cur);

    Json::Value options(Json::objectValue);
    options["fee_rate"] = FLAGS_democrit_feerate_wo_names;
    options["lockUnspents"] = true;

    const auto resp = tm.xayaRpc->walletcreatefundedpsbt (
        Json::Value (Json::arrayValue), outputs, 0, options);

    CHECK (resp.isObject ());
    const auto& psbtVal = resp["psbt"];
    CHECK (psbtVal.isString ());
    chiPart = psbtVal.asString ();
    VLOG (1) << "Funded PSBT:\n" << chiPart;
  }

  /* Second step:  Build a transaction that just has the name input and
     output with the desired name operation.  */
  std::string namePart;
  {
    Json::Value inputs(Json::arrayValue);
    Json::Value cur(Json::objectValue);
    cur["txid"] = nameIn.hash ();
    cur["vout"] = static_cast<Json::Int> (nameIn.n ());
    inputs.append (cur);

    Json::Value outputs(Json::arrayValue);
    cur = Json::Value (Json::objectValue);
    cur[sd.name_address ()] = xaya::ChiAmountToJson (NAME_VALUE);
    outputs.append (cur);

    namePart = tm.xayaRpc->createpsbt (inputs, outputs);

    Json::Value nameOp(Json::objectValue);
    nameOp["op"] = "name_update";
    nameOp["name"] = "p/" + sellerName;
    nameOp["value"] = checker.GetNameUpdateValue ();

    const auto resp = tm.xayaRpc->namepsbt (namePart, 0, nameOp);

    CHECK (resp.isObject ());
    const auto& psbtVal = resp["psbt"];
    CHECK (psbtVal.isString ());
    namePart = psbtVal.asString ();
    VLOG (1) << "PSBT with just the name operation:\n" << namePart;
  }

  /* Third step:  Combine the two PSBTs (CHI and name parts) into a single
     one with both their inputs and outputs.  */
  std::string psbt;
  {
    Json::Value psbts(Json::arrayValue);
    psbts.append (chiPart);
    psbts.append (namePart);

    psbt = tm.xayaRpc->joinpsbts (psbts);
    VLOG (1) << "Final unsigned PSBT:\n" << psbt;
  }

  return psbt;
}

namespace
{

/**
 * Calls walletprocesspsbt on the RPC connection to sign a transaction and
 * parses the expected result into psbt string and complete flag.
 */
std::string
SignPsbt (RpcClient<XayaRpcClient>& rpc, const std::string& psbt,
          bool& complete)
{
  const auto reply = rpc->walletprocesspsbt (psbt);
  CHECK (reply.isObject ());

  const auto& psbtVal = reply["psbt"];
  CHECK (psbtVal.isString ());

  const auto& completeVal = reply["complete"];
  CHECK (completeVal.isBool ());

  complete = completeVal.asBool ();
  return psbtVal.asString ();
}

} // anonymous namespace

bool
Trade::HasReply (proto::ProcessingMessage& reply)
{
  CHECK (isMutable) << "Trade instance is not mutable";

  /* In any state except INITIATED, there is nothing more to do except
     potentially wait (if the state is PENDING).  */
  if (pb.state () != proto::Trade::INITIATED)
    return false;

  InitProcessingMessage (reply);

  /* First we need to handle the seller-data exchange.  We need that done
     before proceeding further in the process in any case.  */
  if (CreateSellerData ())
    {
      CHECK (pb.has_seller_data ());

      auto* sd = reply.mutable_seller_data ();
      *sd = pb.seller_data ();
      sd->clear_name_output ();

      return true;
    }
  if (!pb.has_seller_data ())
    return false;

  /* If we are the seller and don't yet have received a PSBT from the
     counterparty, we need to wait for them.  */
  if (GetOrderType () == proto::Order::ASK && !pb.has_their_psbt ())
    return false;

  /* If we are the seller, have the counterparty's PSBT but not yet ours
     filled in, we sign the PSBT and fill it into our_psbt.  */
  if (GetOrderType () == proto::Order::ASK && !pb.has_our_psbt ())
    {
      CHECK (pb.has_their_psbt ());

      if (!checker->CheckForSellerOutputs (pb.their_psbt (), pb.seller_data ()))
        {
          LOG (WARNING) << "Buyer provided invalid PSBT for the trade";
          return false;
        }

      bool complete;
      const auto psbt = SignPsbt (tm.xayaRpc, pb.their_psbt (), complete);

      if (!checker->CheckForSellerSignature (pb.their_psbt (), psbt,
                                             pb.seller_data ()))
        {
          LOG (WARNING) << "Signing PSBT as seller provided invalid signatures";
          return false;
        }

      switch (GetRole ())
        {
        case proto::Trade::MAKER:
          if (!complete)
            {
              LOG (WARNING)
                  << "We are maker/seller, but the PSBT is not complete yet";
              return false;
            }
          break;

        case proto::Trade::TAKER:
          if (complete)
            {
              LOG (WARNING)
                  << "We are taker/seller and the PSBT is already complete";
              return false;
            }
          break;

        default:
          LOG (FATAL) << "Unexpected role: " << GetRole ();
        }

      pb.set_our_psbt (psbt);
      VLOG (1) << "Our signed PSBT:\n" << pb.our_psbt ();
    }

  /* If we are the buyer and don't yet have a PSBT constructed, do that now
     and share it with the counterparty.  */
  if (GetOrderType () == proto::Order::BID && !pb.has_our_psbt ())
    {
      proto::OutPoint nameIn;
      if (!checker->CheckForBuyerTrade (nameIn))
        {
          LOG (WARNING) << "Seller cannot fulfill the trade";
          return false;
        }

      const auto unsignedPsbt = ConstructTransaction (*checker, nameIn);

      bool complete;
      const auto signedPsbt = SignPsbt (tm.xayaRpc, unsignedPsbt, complete);

      if (!checker->CheckForBuyerSignature (unsignedPsbt, signedPsbt))
        {
          LOG (WARNING) << "Signing PSBT as buyer provided invalid signatures";
          /* ConstructTransaction locked the inputs in our wallet, but we
             are now discarding this transaction.  Make sure to unlock the
             inputs again.  */
          UnlockPsbtInputs (tm.xayaRpc, signedPsbt);
          return false;
        }

      /* CheckForBuyerSignature verifies that all but one inputs are signed.
         Since the initial transaction was unsigned, it cannot be complete.  */
      CHECK (!complete);

      pb.set_our_psbt (signedPsbt);
      VLOG (1) << "Constructed and partially-signed PSBT:\n" << pb.our_psbt ();

      /* If we are maker as well as buyer, then there is an extra hop where
         we share the *unsigned* transaction and still need to wait for
         the counterparty before finishing everything off.  */
      if (GetRole () == proto::Trade::MAKER)
        {
          reply.mutable_psbt ()->set_psbt (unsignedPsbt);
          VLOG (1)
              << "Sharing unsigned PSBT with counterparty:\n"
              << reply.DebugString ();
          return true;
        }
    }

  /* When we made it here, we have in any case filled in our PSBT.  If we are
     the taker, this is the point where we share it with the counterparty
     and from then on just need to wait for network confirmation.  */
  CHECK (pb.has_our_psbt ());
  if (GetRole () == proto::Trade::TAKER)
    {
      reply.mutable_psbt ()->set_psbt (pb.our_psbt ());
      VLOG (1)
          << "Sharing our PSBT with the counterparty as taker:\n"
          << reply.DebugString ();
      pb.set_state (proto::Trade::PENDING);
      return true;
    }

  /* We are the maker, and the case of sharing the unsigned initial PSBT
     as maker/buyer has been handled above already.  This means that now
     we either have both PSBTs and can finalise and broadcast the
     transaction, or we still need to wait.  */
  CHECK_EQ (GetRole (), proto::Trade::MAKER);
  if (!pb.has_their_psbt ())
    return false;

  std::string finalPsbt;
  switch (GetOrderType ())
    {
    case proto::Order::BID:
      {
        Json::Value psbts(Json::arrayValue);
        psbts.append (pb.their_psbt ());
        psbts.append (pb.our_psbt ());
        finalPsbt = tm.xayaRpc->combinepsbt (psbts);
        break;
      }

    case proto::Order::ASK:
      /* As the seller, we received the partially signed transaction
         already and signed *that* ourselves, so our PSBT is the final one.  */
      finalPsbt = pb.our_psbt ();
      break;

    default:
      LOG (FATAL) << "Unexpected order type: " << GetOrderType ();
    }

  VLOG (1) << "Final, fully signed PSBT:\n" << finalPsbt;

  const auto finalised = tm.xayaRpc->finalizepsbt (finalPsbt);
  CHECK (finalised.isObject ());
  const auto& completeVal = finalised["complete"];
  CHECK (completeVal.isBool ());
  if (!completeVal.asBool ())
    {
      LOG (WARNING) << "PSBT is not yet complete:\n" << finalPsbt;
      return false;
    }

  const auto& hexVal = finalised["hex"];
  CHECK (hexVal.isString ());

  const std::string txid = tm.xayaRpc->sendrawtransaction (hexVal.asString ());
  LOG (INFO) << "Broadcasted trade transaction: " << txid;

  pb.set_state (proto::Trade::PENDING);
  return false;
}

void
Trade::Update ()
{
  VLOG (1) << "Updating trade:\n" << pb.DebugString ();
  CHECK (isMutable) << "Trade instance is not mutable";

  /* If a trade is "initialised" for too long, we abandon it.  The processing
     from "initialised" to "pending" should just take a few seconds normally,
     and in particular does not depend on block confirmations.  */
  if (pb.state () == proto::Trade::INITIATED)
    {
      const auto timestampNow = tm.GetCurrentTime ();
      const auto now = Clock::time_point (std::chrono::seconds (timestampNow));
      if (now - GetStartTime () > tm.GetTradeTimeout ())
        {
          LOG (INFO)
              << "Abandoning timed-out trade with " << pb.counterparty ()
              << ": " << GetIdentifier ();
          pb.set_state (proto::Trade::ABANDONED);
        }
      return;
    }

  /* If the trade is pending, we check it against the blockchain to determine
     if it has been confirmed or failed yet.  We ignore any other state.  */
  if (pb.state () != proto::Trade::PENDING)
    return;

  /* First, check the state of this trade's btxid in the g/dem GSP.  If it is
     confirmed with a sufficiently low height (compared to the current block
     height), then we mark the trade as succeeded.  */
  CHECK (pb.has_our_psbt ());
  const auto decoded = tm.xayaRpc->decodepsbt (pb.our_psbt ());
  CHECK (decoded.isObject ());
  const auto& tx = decoded["tx"];
  CHECK (tx.isObject ());
  const auto& btxidVal = tx["btxid"];
  CHECK (btxidVal.isString ());
  const std::string btxid = btxidVal.asString ();

  const auto check = tm.demGsp->checktrade (btxid);
  CHECK (check.isObject ());
  const auto& curHeightVal = check["height"];
  CHECK (curHeightVal.isUInt ());
  const uint64_t curHeight = curHeightVal.asUInt ();
  const auto& dataVal = check["data"];
  CHECK (dataVal.isObject ());
  const auto& stateVal = dataVal["state"];
  CHECK (stateVal.isString ());

  if (stateVal.asString () == "confirmed")
    {
      const auto& confHeightVal = dataVal["height"];
      CHECK (confHeightVal.isUInt ());
      const uint64_t confHeight = confHeightVal.asUInt ();
      CHECK (confHeight <= curHeight);
      if (confHeight + FLAGS_democrit_confirmations <= curHeight + 1)
        {
          LOG (INFO) << "Trade with btxid " << btxid << " is confirmed now";
          pb.set_state (proto::Trade::SUCCESS);
          return;
        }
    }

  /* If the trade is confirmed or even just pending, we know it hasn't been
     double spent, even if we do not yet consider it final.  */
  if (stateVal.asString () != "unknown")
    {
      VLOG (1)
          << "Trade with btxid " << btxid << " is still confirming:\n" << check;
      /* Currently, the trade is not conflicted.  If it was before and then
         e.g. a reorg happened, unset the conflicted height.  */
      pb.clear_conflict_height ();
      return;
    }

  /* If one of the trade's inputs is not available, the trade is conflicted.
     The first time this happens, we remember the block height.  If we then
     advance beyond the required confirmations, we mark it as failed.  */
  const auto& vin = tx["vin"];
  CHECK (vin.isArray ());
  bool conflicted = false;
  for (const auto& in : vin)
    {
      CHECK (in.isObject ());
      const auto& hashVal = in["txid"];
      CHECK (hashVal.isString ());
      const auto& nVal = in["vout"];
      CHECK (nVal.isUInt ());

      /* gettxout can return JSON objects and JSON null, which does not work
         well with the libjson-rpc-cpp generated code.  */
      Json::Value params(Json::arrayValue);
      params.append (hashVal.asString ());
      params.append (nVal.asUInt ());
      const auto utxoData = tm.xayaRpc->CallMethod ("gettxout", params);

      if (utxoData.isNull ())
        {
          VLOG (1)
              << "For trade with btxid " << btxid
              << ", the input " << hashVal << ":" << nVal
              << " has been double spent";
          conflicted = true;
          break;
        }
    }
  if (!conflicted)
    {
      pb.clear_conflict_height ();
      return;
    }

  if (!pb.has_conflict_height ())
    {
      LOG (INFO)
          << "Trade with btxid " << btxid
          << " is conflicted at height " << curHeight;
      pb.set_conflict_height (curHeight);
      return;
    }

  if (pb.conflict_height () + FLAGS_democrit_confirmations <= curHeight + 1)
    {
      LOG (INFO) << "Trade with btxid " << btxid << " is confirmed failed";
      pb.set_state (proto::Trade::FAILED);
    }
}

void
Trade::HandleSuccess () const
{
  if (GetRole () == proto::Trade::MAKER)
    {
      VLOG (1)
          << "Trade against our order " << pb.order ().id ()
          << " succeeded, deleting original order";
      tm.myOrders.RemoveById (pb.order ().id ());

      auto newOrder = pb.order ();
      newOrder.clear_id ();
      newOrder.set_max_units (pb.order ().max_units () - pb.units ());

      if (newOrder.max_units () > 0
            && newOrder.max_units () >= newOrder.min_units ())
        {
          VLOG (1) << "Recreating reduced order:\n" << newOrder.DebugString ();
          tm.myOrders.Add (std::move (newOrder));
        }
    }
}

void
Trade::HandleFailure () const
{
  if (GetRole () == proto::Trade::MAKER)
    {
      VLOG (1)
          << "Releasing our order with ID " << pb.order ().id ()
          << " after trade failure";
      tm.myOrders.Unlock (pb.order ().id ());
    }

  if (pb.seller_data ().has_name_output ())
    {
      VLOG (1)
          << "Unlocking name output for failed sale: "
          << pb.seller_data ().name_output ().DebugString ();
      LockUnspent (tm.xayaRpc, false, pb.seller_data ().name_output ());
    }

  if (GetOrderType () == proto::Order::BID && pb.has_our_psbt ())
    {
      VLOG (1) << "Unlocking inputs for failed sale:\n" << pb.our_psbt ();
      UnlockPsbtInputs (tm.xayaRpc, pb.our_psbt ());
    }
}

/* ************************************************************************** */

TradeManager::TradeManager (State& s, MyOrders& mo, const AssetSpec& as,
                            RpcClient<XayaRpcClient>& x,
                            RpcClient<DemGspRpcClient>& d,
                            const bool startUpdates)
  : state(s), myOrders(mo), spec(as),
    xayaRpc(x), demGsp(d)
{
  if (startUpdates)
    SetupUpdater (GetTradeTimeout ());
}

void
TradeManager::UpdateAndArchiveTrades ()
{
  VLOG (1) << "Running periodic update of trades...";

  std::string account;
  std::vector<proto::TradeState> finalised;
  state.AccessState ([&] (proto::State& s)
    {
      account = s.account ();

      google::protobuf::RepeatedPtrField<proto::TradeState> stillActive;
      for (proto::TradeState& t : *s.mutable_trades ())
        {
          Trade obj(*this, account, t);
          obj.Update ();
          if (obj.IsFinalised ())
            {
              *s.mutable_trade_archive ()->Add () = obj.GetPublicInfo ();
              finalised.emplace_back (std::move (t));
            }
          else
            *stillActive.Add () = std::move (t);
        }

      s.mutable_trades ()->Swap (&stillActive);
    });

  /* If trades got finalised, we need to do some further processing on them,
     e.g. to release locked inputs or to restore the order if we are the maker
     and the trade failed.  */
  for (const auto& t : finalised)
    {
      const Trade obj(*this, account, t);
      switch (t.state ())
        {
        case proto::Trade::ABANDONED:
        case proto::Trade::FAILED:
          obj.HandleFailure ();
          break;
        case proto::Trade::SUCCESS:
          obj.HandleSuccess ();
          break;
        default:
          /* Any other state should not have been finalised!  */
          LOG (FATAL)
              << "Trade with state " << static_cast<int> (t.state ())
              << " has been archived";
        }
    }

  LOG_IF (INFO, !finalised.empty ())
      << "Archived " << finalised.size () << " finalised trades";
}

std::vector<proto::Trade>
TradeManager::GetTrades () const
{
  std::vector<proto::Trade> res;
  state.ReadState ([this, &res] (const proto::State& s)
    {
      for (const auto& t : s.trades ())
        res.push_back (Trade (*this, s.account (), t).GetPublicInfo ());
      for (const auto& t : s.trade_archive ())
        res.push_back (t);
    });

  return res;
}

void
TradeManager::SetupUpdater (const Trade::Clock::duration intv)
{
  updater = std::make_unique<IntervalJob> (intv,
      [this] ()
        {
          UpdateAndArchiveTrades ();
        });
}

int64_t
TradeManager::GetCurrentTime () const
{
  const auto dur = Trade::Clock::now ().time_since_epoch ();
  return std::chrono::duration_cast<std::chrono::seconds> (dur).count ();
}

Trade::Clock::duration
TradeManager::GetTradeTimeout ()
{
  return std::chrono::milliseconds (FLAGS_democrit_trade_timeout_ms);
}

namespace
{

/**
 * Checks if the given order can be taken with the given amount,
 * and that it has in general all the fields necessary and is valid
 * for our purposes (so we can start a trade).
 */
bool
CheckOrder (const proto::Order& o, const Amount units)
{
  if (units > static_cast<Amount> (o.max_units ())
        || units < static_cast<Amount> (o.min_units ()))
    {
      LOG (WARNING)
          << "Cannot take order for " << units << " units:\n"
          << o.DebugString ();
      return false;
    }

  if (!o.has_account () || !o.has_id ()
        || !o.has_asset () || !o.has_type () || !o.has_price_sat ())
    {
      LOG (WARNING) << "Order to take is missing fields:\n" << o.DebugString ();
      return false;
    }

  return true;
}

} // anonymous namespace

bool
TradeManager::TakeOrder (const proto::Order& o, const Amount units,
                         proto::ProcessingMessage& msg)
{
  if (!CheckOrder (o, units))
    return false;

  proto::TradeState data;
  *data.mutable_order () = o;
  data.set_start_time (GetCurrentTime ());
  data.set_units (units);
  data.set_counterparty (o.account ());
  data.set_state (proto::Trade::INITIATED);

  bool ok;
  state.AccessState ([&] (proto::State& s)
    {
      if (data.counterparty () == s.account ())
        {
          LOG (WARNING)
              << "Can't take own order:\n" << data.order ().DebugString ();
          ok = false;
          return;
        }

      Trade t(*this, s.account (), data);

      try
        {
          if (t.HasReply (msg))
            {
              /* This means we were the seller and it filled in the seller
                 data as well.  We still add the "taking_order" field below.  */
            }
          else
            t.InitProcessingMessage (msg);
        }
      catch (const jsonrpc::JsonRpcException& exc)
        {
          LOG (WARNING)
              << "JSON-RPC exception: " << exc.what ()
              << "\nWhile taking order:\n" << data.order ().DebugString ();
          ok = false;
          return;
        }

      t.SetTakingOrder (msg);

      *s.mutable_trades ()->Add () = std::move (data);
      ok = true;
    });

  return ok;
}

bool
TradeManager::OrderTaken (const proto::Order& o, const Amount units,
                          const std::string& counterparty)
{
  if (!CheckOrder (o, units))
    return false;

  proto::TradeState data;
  *data.mutable_order () = o;
  data.set_start_time (GetCurrentTime ());
  data.set_units (units);
  data.set_counterparty (counterparty);
  data.set_state (proto::Trade::INITIATED);

  bool ok;
  state.AccessState ([&data, &ok] (proto::State& s)
    {
      CHECK_EQ (data.order ().account (), s.account ());

      if (data.counterparty () == s.account ())
        {
          LOG (WARNING)
              << "Order taken by ourselves:\n" << data.order ().DebugString ();
          ok = false;
        }
      else
        {
          *s.mutable_trades ()->Add () = std::move (data);
          ok = true;
        }
    });

  return ok;
}

bool
TradeManager::ProcessMessage (const proto::ProcessingMessage& msg,
                              proto::ProcessingMessage& reply)
{
  CHECK (msg.has_counterparty ());

  if (msg.has_taking_order ())
    {
      proto::Order o;
      if (!myOrders.TryLock (msg.taking_order ().id (), o))
        {
          LOG (WARNING)
              << "Counterparty tried to take non-available own order:\n"
              << msg.DebugString ();
          return false;
        }

      if (!OrderTaken (o, msg.taking_order ().units (), msg.counterparty ()))
        {
          LOG (WARNING)
              << "Counterparty cannot take our order:\n"
              << msg.DebugString ();
          myOrders.Unlock (msg.taking_order ().id ());
          return false;
        }

      /* The order has been created now.  In case we have e.g. seller data
         to attach already or a reply to send, this will be handled by normal
         processing below.  */
    }

  bool ok = false;
  state.AccessState ([&] (proto::State& s)
    {
      for (auto& tPb : *s.mutable_trades ())
        {
          Trade t(*this, s.account (), tPb);
          if (!t.Matches (msg))
            continue;
          CHECK (!ok);

          try
            {
              t.HandleMessage (msg);
              if (t.HasReply (reply))
                ok = true;
            }
          catch (const jsonrpc::JsonRpcException& exc)
            {
              LOG (WARNING)
                  << "JSON-RPC exception: " << exc.what ()
                  << "\nWhile processing message:\n" << msg.DebugString ();
              CHECK (!ok);
            }

          break;
        }
    });

  return ok;
}

/* ************************************************************************** */

} // namespace democrit
