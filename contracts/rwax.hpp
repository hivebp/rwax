#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/transaction.hpp>
#include <atomicdata.hpp>
#include <nlohmann/json.hpp>
#include "constants.hpp"
#include "structs.hpp"
#include "tables.hpp"


using namespace std;
using namespace eosio;
using namespace atomicdata;


CONTRACT rwax : public contract {
    public:
        using contract::contract;
        rwax(name receiver, name code, datastream<const char *> ds):
        contract(receiver, code, ds)
        {}

        // Main Actions
        ACTION addstakepool(name pool, symbol reward_token, symbol stake_token); 
        ACTION claim(name staker, asset token);              
        ACTION erasetoken(name authorized_account, asset token);
        ACTION init();
        ACTION redeem(name redeemer, asset quantity);    
        ACTION stake(name staker, asset quantity);            
        ACTION tokenize(name authorized_account, name collection_name, asset maximum_supply, vector<TEMPLATE> templates,
            vector<TRAITFACTOR> trait_factors, string token_name, string token_logo, string token_logo_lg);
        ACTION tokenizenfts(name user, vector<uint64_t> asset_ids);
        ACTION unstake(name staker, asset quantity);        
        ACTION withdraw(vector<asset> tokens, name account);
 
        // Notifications
        [[eosio::on_notify("atomicassets::transfer")]] void receive_nft_transfer(name from, name to, vector<uint64_t> asset_ids, string memo);
        [[eosio::on_notify("*::transfer")]] void receive_any_transfer(name from, name to, asset amount, std::string memo);

    private:

        // Using
        using json = nlohmann::json;

        // Tables
        balances_t      balances        = balances_t(_self, _self.value);
        collections_t   collections     = collections_t(ATOMICASSETS_CONTRACT, ATOMICASSETS_CONTRACT.value);
        config_t        config          = config_t(_self, _self.value);
        templpools_t    templpools      = templpools_t(_self, _self.value);
        tokens_t        tokens          = tokens_t(_self, _self.value);
        traitfactors_t  traitfactors    = traitfactors_t(_self, _self.value);        
        transfers_t     transfers       = transfers_t(_self, _self.value);

        // Functions
        void            add_balances(name account, vector<asset> tokens); 
        asset           calculate_issued_tokens(uint64_t asset_id, int32_t template_id); 
        void            check_collection_auth(name collection_name, name authorized_account);        
        void            check_has_collection_auth(name account_to_check, name collection_name, string error_message);
        name            find_asset_pool(asset token);        
        assetpools_t    get_assetpool(uint64_t symbolraw);
        assets_t        get_assets(name acc);
        float           get_maximum_factor(vector<TRAITFACTOR> trait_factors);        
        vector<asset>   get_prices(uint64_t drop_id);
        schemas_t       get_schemas(name collection_name);
        stakepools_t    get_stakepools(name pool);
        stakes_t        get_stakes(uint64_t symbolraw);
        templates_t     get_templates(name collection_name);
        name            get_token_contract(symbol token_symbol);        
        bool            is_token_supported(name token_contract, symbol token_symbol);    
        void            tokenize_asset(uint64_t asset_id, name receiver);            
        void            withdraw_balances(name account, vector<asset> tokens);        
};