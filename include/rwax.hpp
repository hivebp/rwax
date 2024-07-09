#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/transaction.hpp>
#include "atomicdata.hpp"
#include "nlohmann/json.hpp"

using namespace std;
using namespace eosio;
using namespace atomicdata;

static constexpr name CORE_TOKEN_CONTRACT = name("eosio.token");
static constexpr name RWAX_TOKEN_CONTRACT = name("token.rwax");
static constexpr symbol CORE_SYMBOL = symbol("WAX", 8);

struct TOKEN {
    name   token_contract;
    symbol token_symbol;
};

struct POOL {
    name pool;
    symbol token;
};

struct TEMPLATE {
    int32_t template_id;
    uint32_t max_assets_to_tokonize;
};

struct VALUEFACTOR {
    string value;
    float factor;
};

struct TRAITFACTOR {
    string trait_name;
    float min_value;
    float max_value;
    float min_factor;
    float max_factor;
    vector<VALUEFACTOR> values;
};

ACTION tokenize(
    name authorized_account,
    name collection_name,
    asset maximum_supply,
    vector<TEMPLATE> templates,
    vector<TRAITFACTOR> trait_factors,
    string token_name,
    string token_logo,
    string token_logo_lg
);

CONTRACT rwax : public contract {
public:
    using contract::contract;

    [[eosio::on_notify("*::transfer")]] void receive_transfer(
        name from,
        name to,
        asset amount,
        string memo
    );

    [[eosio::on_notify("atomicassets::transfer")]] void receive_asset_transfer(
        name from,
        name to,
        vector<uint64_t> asset_ids,
        string memo
    );

    ACTION init();

    ACTION tokenize(
        name authorized_account,
        name collection_name,
        asset maximum_supply,
        vector<TEMPLATE> templates,
        vector<TRAITFACTOR> trait_factors,
        string token_name,
        string token_logo,
        string token_logo_lg
    );

    ACTION tokenizenfts(
        name user,
        vector<uint64_t> asset_ids
    );

    ACTION erasetoken(
        name authorized_account,
        asset token
    );

    ACTION withdraw(
        vector<asset> tokens,
        name account
    );

    ACTION redeem(
        name redeemer,
        asset quantity
    );

    ACTION stake(
        name staker,
        asset quantity
    );

    ACTION addstakepool(
        name pool,
        symbol reward_token,
        symbol stake_token
    );

    ACTION unstake(
        name staker,
        asset quantity
    );

    ACTION claim(
        name staker,
        asset token
    );
private:
    asset calculate_issued_tokens(
        uint64_t asset_id, 
        int32_t template_id
    );

    void withdraw_balances(
        name account,
        vector<asset> tokens
    );

    void add_balances(
        name account,
        vector<asset> tokens
    );

    void check_has_collection_auth(
        name account_to_check,
        name collection_name,
        string error_message
    );

    void check_collection_auth(
        name collection_name, 
        name authorized_account
    );

    bool is_token_supported(
        name token_contract,
        symbol token_symbol
    );
    
    void tokenize_asset(
        uint64_t asset_id,
        name receiver
    );

    name get_token_contract(
        symbol token_symbol
    );

    name find_asset_pool(
        asset token
    );

    float get_maximum_factor(
        vector<TRAITFACTOR> trait_factors
    );

    TABLE config_s {
        string version                               = "1.0.0";
        vector<TOKEN> supported_tokens               = {};
        vector<name> stake_pools                     = {};
    };

    typedef singleton <name("config"), config_s>           config_t;
    typedef multi_index <name("config"), config_s>         config_t_for_abi;
    
    struct collections_s {
        name             collection_name;
        name             author;
        bool             allow_notify;
        vector <name>    authorized_accounts;
        vector <name>    notify_accounts;
        double           market_fee;
        vector <uint8_t> serialized_data;

        auto primary_key() const { return collection_name.value; };
    };

    struct assets_s {
        uint64_t         asset_id;
        name             collection_name;
        name             schema_name;
        int32_t          template_id;
        name             ram_payer;
        vector <asset>   backed_tokens;
        vector <uint8_t> immutable_serialized_data;
        vector <uint8_t> mutable_serialized_data;

        uint64_t primary_key() const { return asset_id; };
    };

    struct templates_s {
        int32_t          template_id;
        name             schema_name;
        bool             transferable;
        bool             burnable;
        uint32_t         max_supply;
        uint32_t         issued_supply;
        vector <uint8_t> immutable_serialized_data;

        uint64_t primary_key() const { return (uint64_t) template_id; }
    };

    struct schemas_s {
        name            schema_name;
        vector <FORMAT> format;

        uint64_t primary_key() const { return schema_name.value; }
    };

    vector<asset> get_prices(uint64_t drop_id);

    TABLE tokens_s {
        asset maximum_supply;
        asset issued_supply;
        name authorized_account;
        name collection_name;
        vector<TEMPLATE> templates;

        uint64_t primary_key() const { return (uint64_t) maximum_supply.symbol.code().raw(); } 
    };

    TABLE balances_s {
        name account;
        vector<asset> assets;

        uint64_t primary_key() const { return account.value; };
    };

    TABLE stakes_s {
        name staker;
        asset amount;
        vector<asset> rewarded_tokens;

        uint64_t primary_key() const { return (uint64_t) staker.value; } 
    };

    TABLE stakepools_s {
        symbol reward_token;
        symbol stake_token;

        uint64_t primary_key() const { return (uint64_t) reward_token.code().raw(); } 
    };

    TABLE rewards_s {
        asset amount;

        uint64_t primary_key() const { return (uint64_t) amount.symbol.code().raw(); } 
    };

    TABLE traitfactors_s {
        symbol token;
        vector<TRAITFACTOR> trait_factors;

        uint64_t primary_key() const { return (uint64_t) token.code().raw(); } 
    };

    TABLE templpools_s {
        int32_t template_id;
        uint32_t max_assets_to_tokonize;
        uint32_t currently_tokenized;
        asset token_share;

        uint64_t primary_key() const { return (uint64_t) template_id; } 
    };

    TABLE transfers_s {
        name user;
        vector<uint64_t> assets;

        auto primary_key() const { return user.value; };
    };

    TABLE assetpools_s {
        uint64_t asset_id;

        uint64_t primary_key() const { return (uint64_t) asset_id; } 
    };
    
    typedef eosio::multi_index<name("collections"), collections_s> collections_t;
    typedef eosio::multi_index<name("assets"), assets_s> assets_t;
    typedef eosio::multi_index<name("templates"), templates_s> templates_t;
    typedef eosio::multi_index<name("tokens"), tokens_s> tokens_t;
    typedef eosio::multi_index<name("templpools"), templpools_s> templpools_t;
    typedef eosio::multi_index<name("assetpools"), assetpools_s> assetpools_t;
    typedef eosio::multi_index<name("stakes"), stakes_s> stakes_t;
    typedef eosio::multi_index<name("transfers"), transfers_s> transfers_t;
    typedef eosio::multi_index<name("balances"), balances_s> balances_t;
    typedef eosio::multi_index<name("rewards"), rewards_s> rewards_t;
    typedef eosio::multi_index<name("stakepools"), stakepools_s> stakepools_t;
    typedef eosio::multi_index<name("traitfactors"), traitfactors_s> traitfactors_t;
    typedef multi_index <name("schemas"), schemas_s> schemas_t;
    
    collections_t collections = collections_t(name("atomicassets"), name("atomicassets").value);
    tokens_t tokens = tokens_t(get_self(), get_self().value);
    templpools_t templpools = templpools_t(get_self(), get_self().value);
    transfers_t transfers = transfers_t(get_self(), get_self().value);
    balances_t balances = balances_t(get_self(), get_self().value);
    config_t config = config_t(get_self(), get_self().value);
    traitfactors_t traitfactors = traitfactors_t(get_self(), get_self().value);

    templates_t get_templates(name collection_name) {
        return templates_t(name("atomicassets"), collection_name.value);
    }
    
    assets_t get_assets(name acc) {
        return assets_t(name("atomicassets"), acc.value);
    }
    
    assetpools_t get_assetpool(uint64_t symbolraw) {
        return assetpools_t(get_self(), symbolraw);
    }
    
    stakes_t get_stakes(uint64_t symbolraw) {
        return stakes_t(get_self(), symbolraw);
    }
    
    stakepools_t get_stakepools(name pool) {
        return stakepools_t(get_self(), pool.value);
    }

    schemas_t get_schemas(name collection_name) {
        return schemas_t(name("atomicassets"), collection_name.value);
    }
};
