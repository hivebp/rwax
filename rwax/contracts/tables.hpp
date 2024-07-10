using namespace eosio;
using namespace std;
using namespace atomicdata;

TABLE assetpools_s {
    uint64_t asset_id;

    uint64_t primary_key() const { return (uint64_t) asset_id; } 
};

TABLE balances_s {
    name            account;
    vector<asset>   assets;

    uint64_t primary_key() const { return account.value; };
};

TABLE config_s {
    string          version             = "1.0.0";
    vector<TOKEN>   supported_tokens    = {};
    vector<name>    stake_pools         = {};
};

TABLE rewards_s {
    asset amount;

    uint64_t primary_key() const { return (uint64_t) amount.symbol.code().raw(); } 
};

TABLE stakepools_s {
    symbol reward_token;
    symbol stake_token;

    uint64_t primary_key() const { return (uint64_t) reward_token.code().raw(); } 
};

TABLE stakes_s {
    name            staker;
    asset           amount;
    vector<asset>   rewarded_tokens;

    uint64_t primary_key() const { return (uint64_t) staker.value; } 
};

TABLE templpools_s {
    int32_t     template_id;
    uint32_t    max_assets_to_tokonize;
    uint32_t    currently_tokenized;
    asset       token_share;

    uint64_t primary_key() const { return (uint64_t) template_id; } 
};

TABLE tokens_s {
    asset               maximum_supply;
    asset               issued_supply;
    name                authorized_account;
    name                collection_name;
    vector<TEMPLATE>    templates;

    uint64_t primary_key() const { return (uint64_t) maximum_supply.symbol.code().raw(); } 
};

TABLE traitfactors_s {
    symbol              token;
    vector<TRAITFACTOR> trait_factors;

    uint64_t primary_key() const { return (uint64_t) token.code().raw(); } 
};


TABLE transfers_s {
    name                user;
    vector<uint64_t>    assets;

    uint64_t primary_key() const { return user.value; };
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

struct collections_s {
    name             collection_name;
    name             author;
    bool             allow_notify;
    vector <name>    authorized_accounts;
    vector <name>    notify_accounts;
    double           market_fee;
    vector <uint8_t> serialized_data;

    uint64_t primary_key() const { return collection_name.value; };
};


struct schemas_s {
    name            schema_name;
    vector <FORMAT> format;

    uint64_t primary_key() const { return schema_name.value; }
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

// Singletons
typedef eosio::singleton <name("config"), config_s>           config_t;
typedef eosio::multi_index <name("config"), config_s>         config_t_for_abi;    
  
// Multi Index Tables
typedef eosio::multi_index<name("assetpools"), assetpools_s>        assetpools_t;    
typedef eosio::multi_index<name("assets"), assets_s>                assets_t;
typedef eosio::multi_index<name("balances"), balances_s>            balances_t;
typedef eosio::multi_index<name("collections"), collections_s>      collections_t;
typedef eosio::multi_index<name("rewards"), rewards_s>              rewards_t;
typedef multi_index <name("schemas"), schemas_s>                    schemas_t;
typedef eosio::multi_index<name("stakepools"), stakepools_s>        stakepools_t;
typedef eosio::multi_index<name("stakes"), stakes_s>                stakes_t;
typedef eosio::multi_index<name("templates"), templates_s>          templates_t;
typedef eosio::multi_index<name("templpools"), templpools_s>        templpools_t;
typedef eosio::multi_index<name("tokens"), tokens_s>                tokens_t;
typedef eosio::multi_index<name("traitfactors"), traitfactors_s>    traitfactors_t;
typedef eosio::multi_index<name("transfers"), transfers_s>          transfers_t;
    