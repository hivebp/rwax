#include "rwax.hpp"
#include "functions.cpp"
#include "on_notify.cpp"

//contractName: rwax

ACTION rwax::addstakepool(
    name pool,
    symbol reward_token,
    symbol stake_token
) {
    require_auth(get_self());

    stakepools_t stakepools = get_stakepools(pool);

    check( stakepools.find(pool.value) == stakepools.end(), "Pool already exists" );

    stakepools.emplace(get_self(), [&](auto& _pool) {
        _pool.reward_token = reward_token;
        _pool.stake_token = stake_token;
    });

    config_s current_config = config.get();
    current_config.stake_pools.push_back(pool);
    config.set(current_config, get_self());
}


ACTION rwax::claim(
    name staker,
    asset token
) {
    require_auth(staker);

    stakes_t    token_stakes    = get_stakes(token.symbol.code().raw());
    auto        stake_itr       = token_stakes.require_find(staker.value, "Stake not found");

    for (asset token : stake_itr->rewarded_tokens) {
        name contract = get_token_contract(token.symbol);
        transfer_tokens(staker, token, contract, "Claiming staking rewards");
    }

    token_stakes.erase(stake_itr);

    token_stakes.modify(stake_itr, same_payer, [&](auto& _item) {
        _item.rewarded_tokens = {asset(0, CORE_SYMBOL)};
    });
}


ACTION rwax::erasetoken(
    name authorized_account,
    asset token
) {
    require_auth(authorized_account);

    auto token_itr = tokens.require_find(token.symbol.code().raw(), "Token not found");

    check(token_itr->authorized_account == authorized_account, "No authorized to erase Token");

    for (TEMPLATE tmpl : token_itr->templates) {
        auto template_pool_itr = templpools.find(tmpl.template_id);
        if (template_pool_itr != templpools.end()) {
            templpools.erase(template_pool_itr);
        }
    }

    assetpools_t asset_pools = get_assetpool(token.symbol.code().raw());

    auto apool_itr = asset_pools.begin();

    vector<uint64_t> asset_ids = {};
    while (apool_itr != asset_pools.end()) {
        asset_ids.push_back(apool_itr->asset_id);
        asset_pools.erase(apool_itr);
        apool_itr = asset_pools.begin();
    }

    transfer_tokens(authorized_account, token_itr->maximum_supply - token_itr->issued_supply, RWAX_TOKEN_CONTRACT, "rWAX: Erasing Token");

    if (asset_ids.size() > 0) {
        transfer_nfts(authorized_account, asset_ids, string("rWAX: Erasing Token"));
    }

    tokens.erase(token_itr);
}


ACTION rwax::init() {
    require_auth(get_self());
    config.get_or_create(get_self(), config_s{});
}


ACTION rwax::redeem(
    name    redeemer,
    asset   quantity
) {
    require_auth(redeemer);

    auto balance_itr = balances.require_find(redeemer.value, "No balance object found");

    check(quantity.amount > 0, "Must redeem positive amount");

    vector<asset> assets = {quantity};

    withdraw_balances(redeemer, assets);

    auto        token_itr               = tokens.require_find(quantity.symbol.code().raw(), "Token not found");
    asset       issued_supply           = token_itr->issued_supply;
    uint32_t    total_assets_tokenized  = 0;

    for (TEMPLATE templ : token_itr->templates) {
        auto templ_itr = templpools.find(templ.template_id);
        total_assets_tokenized += templ_itr->currently_tokenized;
    }

    assetpools_t    asset_pools = get_assetpool(quantity.symbol.code().raw());
    auto            apool_itr   = asset_pools.begin();

    check(apool_itr != asset_pools.end(), "No assets available");

    name        asset_pool          = find_asset_pool(issued_supply);
    assets_t    asset_pool_assets   = get_assets(asset_pool);
    auto        asset_itr           = asset_pool_assets.find(apool_itr->asset_id);
    auto        template_pool_itr   = templpools.find(asset_itr->template_id);

    templpools.modify(template_pool_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized --;
    });

    asset required_amount = calculate_issued_tokens(apool_itr->asset_id, asset_itr->template_id);

    if (required_amount.amount < quantity.amount) {
        check(false, ("Invalid amount. Must transfer exactly " + required_amount.to_string()).c_str());
    }

    asset_pools.erase(apool_itr);

    if (asset_pool == get_self()) {
        transfer_nfts(redeemer, {asset_itr->asset_id}, string("Redeeming from RWAX"));
    } else {
        action(
            permission_level{get_self(), name("active")},
            asset_pool,
            name("redeem"),
            make_tuple(
                redeemer,
                asset_itr->asset_id
            )
        ).send();
    }

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply -= required_amount;
    });
}


ACTION rwax::stake(
    name staker,
    asset quantity
) {
    require_auth(staker);

    auto balance_itr = balances.require_find(staker.value, "No balance object found");

    check(quantity.amount > 0, "Must redeem positive amount");

    vector<asset> assets = {};

    assets.push_back(quantity);

    withdraw_balances(staker, assets);

    stakes_t token_stakes = get_stakes(quantity.symbol.code().raw());

    auto stake_itr = token_stakes.find(staker.value);

    if (stake_itr == token_stakes.end()) {
        token_stakes.emplace(get_self(), [&](auto& _stake) {
            _stake.staker           = staker;
            _stake.amount           = quantity;
            _stake.rewarded_tokens  = {asset(0, CORE_SYMBOL)};
        });
    } else {
        token_stakes.modify(stake_itr, staker, [&](auto& modified_item) {
            modified_item.amount += quantity;
        });
    }
}


ACTION rwax::tokenize(
    name                authorized_account,
    name                collection_name,
    asset               maximum_supply,
    vector<TEMPLATE>    templates,
    vector<TRAITFACTOR> trait_factors,
    string              token_name,
    string              token_logo,
    string              token_logo_lg
) {
    check_collection_auth(collection_name, authorized_account);

    check(maximum_supply.amount > 0, "Must provide positive supply");

    templates_t collection_templates = get_templates(collection_name);
    
    uint32_t total_assets_to_tokenize = 0;
    for (TEMPLATE templ : templates) {
        check(templ.max_assets_to_tokonize > 0, "Need to provide a maximum number of assets to tokenize");
        total_assets_to_tokenize += templ.max_assets_to_tokonize;
    }
    
    for (TEMPLATE templ : templates) {
        int32_t template_id     = templ.template_id;
        auto    template_itr    = collection_templates.find(template_id);

        if (template_itr == collection_templates.end()) {
            check(false, ("No template with this ID exists: " + to_string(template_id)).c_str());
        }

        if (template_itr->max_supply > 0) {
            check(templ.max_assets_to_tokonize <= template_itr->max_supply,
                "Templates actual supply is less than given max supply");
        }

        auto template_pool_itr = templpools.find(template_id);

        if (template_pool_itr != templpools.end()) {
            check(false, ("Template " + to_string(template_id) + " has already been tokenized").c_str());
        }

        asset template_supply = asset(maximum_supply.amount * (templ.max_assets_to_tokonize / total_assets_to_tokenize), maximum_supply.symbol);

        templpools.emplace(authorized_account, [&](auto& new_pool) {
            new_pool.template_id            = template_id;
            new_pool.max_assets_to_tokonize = templ.max_assets_to_tokonize;
            new_pool.currently_tokenized    = 0;
            new_pool.token_share            = template_supply;
        });
    }

    auto token_itr = tokens.find(maximum_supply.symbol.code().raw());

    check(token_itr == tokens.end(), "Symbol already exists");

    asset issued_supply = maximum_supply;
    issued_supply.amount = 0;

    for (TRAITFACTOR factor : trait_factors) {
        check(factor.min_factor >= 1, "Minimum Factor must be >= 1");
        check(factor.max_factor >= 1, "Maximum Factor must be >= 1");
        check(factor.max_factor >= factor.min_factor, "Maximum Factor must be >= Minimum Factor");
        for (VALUEFACTOR value_factor : factor.values) {
            check(value_factor.factor >= 1, "Value factor must be >= 1");
            check(value_factor.factor <= factor.max_factor, "Value factor must be <= maximum factor");
        }
    }

    if (trait_factors.size() > 0) {
        traitfactors.emplace(authorized_account, [&](auto& new_factor) {
            new_factor.token            = maximum_supply.symbol;
            new_factor.trait_factors    = trait_factors;
        });
    }

    tokens.emplace(authorized_account, [&](auto& new_token) {
        new_token.maximum_supply        = maximum_supply;
        new_token.issued_supply         = issued_supply;
        new_token.collection_name       = collection_name;
        new_token.authorized_account    = authorized_account;
        new_token.templates             = templates;
    });

    action(
        permission_level{get_self(), name("active")},
        RWAX_TOKEN_CONTRACT,
        name("create"),
        make_tuple(
            get_self(),
            maximum_supply,
            string(token_name),
            string(token_logo),
            string(token_logo_lg)
        )
    ).send();

    action(
        permission_level{get_self(), name("active")},
        RWAX_TOKEN_CONTRACT,
        name("issue"),
        make_tuple(
            get_self(),
            maximum_supply,
            string("Initialize New Token")
        )
    ).send();
}


ACTION rwax::tokenizenfts(
    name user,
    vector<uint64_t> asset_ids
) {
    require_auth(user);

    auto                transfer_itr    = transfers.require_find(user.value, "No assets found");
    vector<uint64_t>    new_assets      = transfer_itr->assets;

    for (uint64_t asset_id : asset_ids) {
        if(std::find(transfer_itr->assets.begin(), transfer_itr->assets.end(), asset_id) == transfer_itr->assets.end()) { 
            check(false, ("Asset " + to_string(asset_id) + " not found in Transfer.").c_str());
        }

        auto asset_ptr = std::find(new_assets.begin(), new_assets.end(), asset_id);
        if (asset_ptr != new_assets.end()) {
            new_assets.erase(asset_ptr);
        }

        tokenize_asset(asset_id, user);
    }

    if (new_assets.size() == 0) {
        transfers.erase(transfer_itr);
    } else {
        transfers.modify(transfer_itr, get_self(), [&](auto& new_transfer) {
            new_transfer.assets = new_assets;
        });
    }
}


ACTION rwax::unstake(
    name staker,
    asset quantity
) {
    require_auth(staker);

    stakes_t    token_stakes    = get_stakes(quantity.symbol.code().raw());
    auto        stake_itr       = token_stakes.require_find(staker.value, "Stake not found");

    check(stake_itr->amount.amount >= quantity.amount, "Overdrawn Balance");

    transfer_tokens(staker, quantity, RWAX_TOKEN_CONTRACT, "Returning staked amount");

    if (quantity.amount == stake_itr->amount.amount) {
        for (asset token : stake_itr->rewarded_tokens) {
            name contract = get_token_contract(token.symbol);

            transfer_tokens(staker, token, contract, "Returning staking rewards");
        }

        token_stakes.erase(stake_itr);
    } else {
        token_stakes.modify(stake_itr, staker, [&](auto& modified_item) {
            modified_item.amount -= quantity;
        });
    }
}


ACTION rwax::withdraw(
    vector<asset>   tokens,
    name            account
) {
    require_auth(account);

    auto& balance_itr = balances.get(account.value, "Balance not Found");

    withdraw_balances(account, tokens);

    for (asset token : tokens) {
        name contract = get_token_contract(token.symbol);
        transfer_tokens(account, token, contract, "NFTHive craft Balance Withdrawal");
    }
}