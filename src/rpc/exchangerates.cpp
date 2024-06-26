// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server_util.h>

#include <assetsdir.h>
#include <exchangerates.h>
#include <rpc/register.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <txmempool.h>

using node::NodeContext;

static RPCHelpMan getfeeexchangerates()
{
    return RPCHelpMan{"getfeeexchangerates",
                "\nReturns a map of assets with their current exchange rates, for use in valuating fee payments.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "rates", "A table mapping asset tag to rate of exchange for one unit of the asset in terms of the reference fee asset."},
                    }},
                RPCExamples{
                    HelpExampleCli("getfeeexchangerates", "")
                  + HelpExampleRpc("getfeeexchangerates", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue response = UniValue{UniValue::VOBJ};
    UniValue rates = UniValue{UniValue::VOBJ};
    for (auto rate: ExchangeRateMap::GetInstance()) {
        rates.pushKV(rate.first.GetHex(), rate.second.scaledValue);
    }
    response.pushKV("rates", rates);
    return response;
},
    };
}

static RPCHelpMan setfeeexchangerates()
{
    return RPCHelpMan{"setfeeexchangerates",
                "\nPrivileged call to set the set of accepted assets for paying fees, and the exchange rate for each of these assets.\n",
                {
                    {"rates", RPCArg::Type::OBJ_USER_KEYS, RPCArg::Optional::NO, "A table mapping asset tag to rate of exchange for one unit of the asset in terms of the reference fee asset.",
                        {
                            {"asset", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The asset hex is the key, the numeric amount (can be string) is the value"},
                        }
                    },
               },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("setfeeexchangerates", "")
                  + HelpExampleRpc("setfeeexchangerates", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue ratesField = request.params[0].get_obj();
    std::map<std::string, UniValue> rates;
    ratesField.getObjMap(rates);
    ExchangeRateMap exchangeRateMap = ExchangeRateMap::GetInstance();
    for (auto rate : rates) {
        CAsset asset = GetAssetFromString(rate.first);
        if (asset.IsNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Unknown label and invalid asset hex: %s", asset.GetHex()));
        }
        CAmount newRateValue = rate.second.get_int();
        exchangeRateMap[asset] = newRateValue;
    } 
    EnsureAnyMemPool(request.context).RecomputeFees();
    return NullUniValue;
},
    };
}

void RegisterExchangeRatesRPCCommands(CRPCTable &t)
{
// clang-format off

static const CRPCCommand commands[] =
{ //  category              actor (function)
  //  --------------------- ------------------------
    { "exchangerates",      &getfeeexchangerates,                  },
    { "exchangerates",      &setfeeexchangerates,                  },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
