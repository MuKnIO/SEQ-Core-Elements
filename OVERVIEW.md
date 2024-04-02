# No Coin: Technical Overview

## Introduction

### Abstract

There should be no specific transaction fee currency on the sidechain. Thus, users can propose transactions to block signers with the fee expressed as any amount of any asset issued on the sidechain. In the most common expected use cases, transaction fees on the sidechain would ideally be paid as a fraction of the same asset(s) being transferred.

### Motivation

In many blockchains, the requirement for users to have a “gas bank” in a native cryptocurrency (or peg) in order to transfer any other token creates barriers to entry and introduces frictions in user experience, preventing a broader network effect. In Sequentia, block proposers have incentives to accept any token as long as it has a recognized value and sufficient liquidity. They will retrieve and compare fee values by querying price data from CEXs or DEX oracles. If a transaction is taking too long to be included in a block or seems like it might never be included, the user may broadcast a new one with Replace-by-fee. To facilitate users’ choice, every block proposer may also signal the list of tokens that will be accepted according to a selection dictated by purely free-market logic. This freedom also improves scalability since there will be far fewer transactions made with the only purpose of providing “gas” to a wallet, which implies higher transaction costs and pollution for the network (UTXO dust). 

### Roles

- Users
- Node operators
- Block signers
- Block proposers

## Specification

Users will be able to specify the asset used to pay fees when constructing a transaction. If left unspecified, the wallet will default to the asset being sent in the transaction. 

Node operators will be able to assign weight values to issued assets on the network using a new RPC named `setfeeexchangerates`. These exchange rates are then used by the mempool to prioritize transactions during block assembly. If the exchange rate for certain asset has not been specified by the node operator, then it is considered blacklisted, and all transactions using that asset for fee payment will not be added to their mempool nor relayed to other nodes.

To assist wallets with fee asset selection, nodes will expose their current valuations with a new RPC named `getfeeexchangerates`. The input of `setfeeexchangerates` and the output of `getfeeexchangerates` will both have the same schema. Additionally, node operators will be able to statically configure their exchange rates using a JSON config file which also will use the same schema.

For the initial prototype, the schema will simply be a key value map, where the keys are the asset identifiers and the values are the weight value. An asset identifier can either be the hex id or a label specified by the user using Element's `assetdir` parameter. The weight value is a 64-bit integer[^1] that will be scaled by a factor of one billion to enable high precision without resorting to floating point numbers.

Example:

```json
{ 
      "S-BTC": 1000000000,
      "USDt": 100000000,
      "33244cc19dd9df0fd901e27246e3413c8f6a560451e2f3721fb6f636791087c7": 90045
}
```

[^1]: See section on [overflow protections](#overflow-protection) for how this interacts with max total number of coins enforced by consensus code.

## Implementation

The No Coin feature will be implemented as an extension to the Elements blockchain platform, which is itself an extension of Bitcoin. The most relevant feature that Elements adds to Bitcoin is asset issuance, which enables new types of assets to be issued and transferred between network participants. 

Elements also uses a federation of block signers as a low-cost alternative to proof-of-work mining process. This interacts positively with No Coin since it increases the predictability of whether a transaction will be accepted in a given block. Wallets can use the block signer's published exchange rates to perform fee estimations, and can infer the schedule of block proposers based on the deterministic round robin algorithm used to select the next proposer.

All this being said, Elements enforces that transaction fees are paid in what it refers to as the policy asset, which in most cases is a pegged Bitcoin asset. Consequently, No Coins requires that this restriction be removed, which will require a number of changes to the core code.

### Transaction

No structural changes to the transaction model will need to be made since Elements already requires fees to be explicitly specified as a transaction output. And since the output already has an asset field, it will just be a matter of softcoding its value rather than defaulting to the chain’s native asset.

The hardcoding of the fee asset can be found in [src/rpc/rawtransaction_util.cpp#L295-L297](https://github.com/ElementsProject/elements/blob/2d298f7e3f76bc6c19d9550af4fd1ef48cf0b2a6/src/rpc/rawtransaction_util.cpp#L295-L297).

To softcode it, we will need to make changes to the fee output parser found in [src/rpc/rawtransaction_util.cpp#L321-L327](
https://github.com/ElementsProject/elements/blob/2d298f7e3f76bc6c19d9550af4fd1ef48cf0b2a6/).

### Mempool

There are both structural and behavioral changes that need to be made to the mempool in order for transactions to be correctly valuated in the presence of transactions paid with different assets.

Just as with transaction construction described before, the mempool must use the softcoded asset specified in the fee output rather than the chain's hardcoded native asset. The hardcoding of the fee asset can be found in [src/validation.cpp#L885-L886](https://github.com/ElementsProject/elements/blob/2d298f7e3f76bc6c19d9550af4fd1ef48cf0b2a6/src/validation.cpp#L885-L886).

It also must separate the fee value from the fee amount. This is where structural changes are required. The `CtxMemPoolEntry` class requires two additional fields[^2] `nFeeAsset` and `nFeeAmount` to be added after [src/txmempool.h#L98](https://github.com/ElementsProject/elements/blob/2d298f7e3f76bc6c19d9550af4fd1ef48cf0b2a6/src/txmempool.h#L98).

`nFeeAsset` is the asset used to pay the transaction fees, and `nFeeAmount` is the amount paid in said asset. `nFee` is insufficient on its own because it needs to be updated whenever the exchange rates change using the `setfeeexchangerates` RPC. When this happens, the fees of all transactions currently in the mempool must be recomputed. Since transactions can sit in a mempool indefinitely, it's important that transactions with depreciated fee assets are evicted. Likewise, transactions with appreciating fee assets should be bumped in priority in order to maximize the value of fee rewards to block proposers.

Recomputing fees is non-trivial because transactions are weighted by not only their own value but also by their ancestors and descendents. Fortunately, there is a similar functionality already in place for prioritizing transactions based on off-chain payments, so the solution will end up looking very similar to the `CtxMemPool:PrioritiseTransaction` method defined at [src/txmempool.cpp#L1003-L1031](https://github.com/ElementsProject/elements/blob/2d298f7e3f76bc6c19d9550af4fd1ef48cf0b2a6/src/txmempool.cpp#L1003-L1031).

[^2]: In `CtxMempoolEntry`, the fee asset and amount can be retrieved from the transaction reference, so both of these values are effectively a cache to avoid expensive parent transaction lookups. The performance impact has not been measured, but since Bitcoin and Elements both cache `nFee`, it seemed rational to follow suit and cache these values as well.

### Price server

In the simplest case, a block signer can maintain a static mapping of assets to exchange rate values which they configure when setting up their node and never change after. Perhaps they might want to add a few assets as they become available, which they can do by editing their JSON config file and restarting their node, but otherwise leave it the same.

As the value of the network grows, however, block signers will want to be able to update this exchange rate map dynamically and automatically. Thus the need for a price feed, an external service which queries one or many exchanges to find the current market value of a given asset, and then feeds this value to the node via the aforementioned `setfeeexchangerates` RPC.

Being an external service and not integrated into the node, the pricing algorithm can be as simple or as complicated as node operators desire. To start, it may just compute the median of the exchange values over a certain interval of time. In the future, it might store historical data and use it to compute volatility and liquidity.

Sequentia will provide a price server as an example and baseline for node operators to extend as needed. The complexity of the pricing algorithm will scale with the activity of the network, and so our initial goals are for it to be simple and understandable for the most common use case.

### Exchange rate RPCs


### Changes to existing RPCs

Of course, none of these features are meaningful without being exposed in some way to network participants. To that end, we will be extending the existing RPCs to enable specifying which asset fees are paid with, and change defaults to be consistent with this new capability.

Broadly speaking, there are two categories of RPCs that need to be changed: RPCs which create and/or modify transaction fees, and read-only RPCs that provide information about those fees. The former represent the bulk of the work, and the latter should be comparatively straightforward as it will just be a matter of plucking new information from existing data.

In the former category are:

| Category | Name | Changes |
| -------- | ---- | ------- |
| `rawtransactions` | `createrawtransaction` | Add `fee_asset` field to specify the asset used for fee payment |
| `rawtransactions` | `fundrawtransaction` | Add `fee_asset` field to specify the asset used for fee payment |
| `wallet` | `sendtoaddress` | Add `fee_asset` field to specify the asset used for fee payment, otherwise default to the asset being sent in the transaction |

And in the latter:

| Category | Name | Changes |
| -------- | ---- | ------- |
| `wallet` | `gettransaction` | Add a `fee_asset` field in the result, and in each of the details |
| `wallet` | `listtransactions` | In each of the returned transactions, add a `fee_asset` field |
...

## Examples

## Appendix

### Chain parameters

### Bootstrapping

### Overflow protection

### Interaction with confidential transactions
