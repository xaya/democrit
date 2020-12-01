# Democrit

Democrit is a protocol and system for executing [**atomic
trades**](https://github.com/xaya/xaya/blob/master/doc/xaya/trading.md)
on the XAYA platform.  This allows players to trade their game assets for
cryptocurrency (CHI) in a fully trustless manner.

With the Democrit project, game developers have all the tools
ready made for integrating an easy-to-use market place powered by atomic
trades into their games.  There is no need to reimplement low-level details
of the atomic transactions in their game manually.

## Overview

At least for now, Democrit implements *interactive* trades.  This means that
both participants of a trade (buyer and seller) are online at the same time
(e.g. while playing the game itself), and communicate with each other while
negotiating and finalising the trade.  This is the simplest and most
flexbile way to do atomic trades, but in theory it is also possible
to do non-interactive trades with XAYA (as described in the general
documentation for [atomic
trades](https://github.com/xaya/xaya/blob/master/doc/xaya/trading.md)).

Each player or trader that is currently online has their own list of orders
that they are willing to perform, e.g. sell 10 gold coins for 5 CHI each,
or buy a Vorpal sword for 100 CHI.  These orders are published through
a broadcast system, e.g. over XMPP with [XID](https://github.com/xaya/xid).
Other players can then choose an order they would like to accept.

At that point in time, Democrit will (automatically) negotiate and finalise
the on-chain transaction that finishes the trade through a direct communiation
(e.g. again via XMPP messages) between the two trading parties.

## Integration with Games

Democrit takes care of most of the underlying logic for handling
general atomic trades on XAYA.  To launch a decentralised market for
assets of a particular game, the game developer (or in fact any
interested developer) just needs to implement game-specific functions
that tell Democrit

- what tradable assets there are in the game,
- what move data corresponds to a particular transfer of those assets, and
- whether or not a given user can send certain assets in the current game state.

**Democrit assumes that once some assets can be transferred by an account,
this will stay true at least until the next `name_update` of that account.**
In other words, it must not be possible for someone to "spend" tradable
assets without doing an explicit move.  If that is possible, then they could
start a trade where the buyer may not receive their assets even though
the transaction is crafted and sent as designed!

## 'Free Option' Problem

A common issue with various kinds of decentralised markets based on atomic
transactions is the *free-option problem*.  Even though an atomic trade is in
general safe and trustless because it will either execute completely or not
(so it is not possible e.g. for the seller to send the item and not receive
money), one of the two parties will always be "last" for signing the transaction
and can thus at the end decide whether or not to execute the trade;
they can perhaps even wait for some time and see how the market moves
in the mean time, and then only execute the trade if it is beneficial for them.

In the context of XAYA games, this is most likely not as big an issue
as for e.g. decentralised cryptocurrency exchanges.  But one potential issue
is that once the first party has signed a trade, they do not know when exactly
or if at all the second party will counter-sign and finish the transaction.

Thus they will have to wait for some time to see if the trade goes through,
and potentially cancel the original trade and re-negotiate it with
another party instead.  For this to work properly, we need two things:

### Ability to Cancel

Even after signing the first "half" of an atomic transaction, it must
be possible to cancel the trade and invalidate that signature if the
trade has not gone through yet.  In XAYA, this is easily possible by
simply double-spending one of the inputs for the original transaction.

This can be done by the seller through updating their account name
(e.g. just to `{}`), or by the buyer by sending one of the CHI inputs
used back to themselves.

In the context of Democrit, the **taker will be the party that signs first**.
If the maker would sign first, there would be a potentially long
period of time during which the order has to be removed from the public
orderbook, but it is not clear whether or not it is going through; this
could be abused for DoS attacks thinning out the orderbooks.
When the taker is required to sign first, the maker only needs to temporarily
remove the order for a couple of seconds (how long it takes to run the
automated trade negotiation), and can then be (mostly) sure that it will
go through since they finalised and broadcast the transaction themselves.
And if the taker is not providing their signatures within a few seconds
(which will only be due to either rare technical issues or malice),
the maker can just abandon the trade without consequences to either party.

### Tracking of a Trade

When the first party has sent their half of a trade, they need to be able
to track the progress of it.  In other words, they need to watch the
blockchain and notice when the counter-signed transaction has been broadcast
and confirmed.  (Even more importantly, they need to know if the trade
*has not* been finished after some time, so that they can cancel it and
try again with a different party.)

Since they do not have the final transaction, they also do not know the
txid of it yet (at least not for non-segwit transactions).  Thus, Democrit
uses a custom dApp / GSP on XAYA to track executed trades.  The user preparing
the transaction with a move to send game assets
will simply include another "move" for Democrit itself, with a unique ID:

    {
      "g":
        {
          "main game": "send gold coins",
          "dem": {"t": "abcxyz"}
        }
    }

Here, `abcxyz` is a string that the taker chooses, which should
be unique at least for their name.  Then, Democrit will track all
such trades that have gone through, and the taker can query the Democrit game
state to see if the particular ID has executed or not.

## Transaction Fees

There are various ways in which payment of the transaction fee could be
structured.  For instance, the taker of an order could be required to pay it,
or always the seller (as is typical on various other markets for blockchain
assets).

However, for Democrit it seems most suitable if **always the buyer pays
transaction fees**.  Since the buyer is the one funding the transaction
and also in control of how many inputs there will be (and thus how large
the transaction will end up), it makes the most sense.

Note in this context that transaction fees for trades on XAYA will likely
be negligible.  They are comparable to gas fees on Ethereum-based market
places, and *not* the same thing as e.g. a 2% market fee typically
paid by sellers.

## Orderbooks

Each trader in a Democrit market has a list of their own orders they
are willing to execute.  These orders are published regularly to an XMPP
channel (or some other broadcast), so that everyone subscribed to the channel
can construct the *global* order book.

Since all trades are done interactively and thus everyone has to be online,
we require orders to be published frequently (e.g. once every ten minutes) for
each user.  Orders of users who have not published an update in e.g. the
last half an hour will "expire" and no longer be taken into account by
the other traders.  If a user disconnects from the broadcast channel,
their orders will also be removed immediately (e.g. upon receipt of the
unavailable XMPP presence).

## Execution of a Trade

Once two users want to execute a trade (e.g. the seller posted an
"ask" order and a buyer wants to take it), they establish a direct
communication channel between each other.  Via this communcation channel,
the following steps are then performed in order to finalise the trade:

1. The taker initiates the trade by contacting the maker and telling
   them the order and exact amount of asset they are interested in.
   They also send a string for use as trade ID in the `g/dem` move.  This
   string should be unique for their name (but need not be globally unique).
1. If the taker is the seller, they also send two addresses of their wallet,
   one for the name output and one for receiving the payment in CHI.
1. If the maker is the seller, they reply with those two addresses
   in a follow-up message.
1. The buyer constructs the unsigned transaction based on the shared data,
   their own inputs and change address, and the move data for the trade.
   They also sign their inputs to the transaction and store the
   signatures locally.
1. If the buyer is the taker, they add the signatures and share the
   partially signed transaction with the seller.  If the buyer is the maker,
   they share the *unsigned* transaction with the seller.
1. The seller verifies that the payment and name output are as expected,
   and that the current UTXO of their name is an input to the transaction.
   Then they sign just that single input.
1. If the seller is the maker, the transaction is now fully signed and
   can be broadcast.  If the seller is the taker, they send the partially
   signed transaction back to the buyer.
1. If the buyer is the maker, they add the previously stored signatures
   into the transaction and broadcast it.

The taker marks the order as "in progress" when they send their partial
transaction to the maker, and then watches the Democrit GSP to see when the
trade goes through based on their chosen trade ID.  If it takes too long,
they can cancel any time by just double spending one of their inputs
(CHI or name).
The maker constructs and broadcasts the final transaction, so they can
track the progress simply through their wallet.  Thus they can also show the
trade as "in progress" until it is confirmed.

Until the trade has gone through, both participants can check all the
inputs of the transaction on the network to detect a potential double spend
of any of them.  If this happens, they know the trade failed and their
client can show it as such.
