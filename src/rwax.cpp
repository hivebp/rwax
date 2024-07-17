#include <rwax.hpp>

using json = nlohmann::json;

ACTION rwax::createtoken(
    name authorized_account,
    name collection_name,
    asset maximum_supply,
    name contract,
    vector<TEMPLATE> templates,
    vector<TRAITFACTOR> trait_factors,
    string token_name,
    string token_logo,
    string token_logo_lg
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
        int32_t template_id = templ.template_id;
        auto template_itr = collection_templates.find(template_id);

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
            new_pool.template_id = template_id;
            new_pool.max_assets_to_tokonize = templ.max_assets_to_tokonize;
            new_pool.currently_tokenized = 0;
            new_pool.token_share = template_supply;
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
            new_factor.token = maximum_supply.symbol;
            new_factor.trait_factors = trait_factors;
        });
    }

    tokens.emplace(authorized_account, [&](auto& new_token) {
        new_token.maximum_supply = maximum_supply;
        new_token.issued_supply = issued_supply;
        new_token.contract = contract;
        new_token.collection_name = collection_name;
        new_token.authorized_account = authorized_account;
        new_token.templates = templates;
    });

    if (contract == RWAX_TOKEN_CONTRACT) {
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
    } else {
        auto balance_itr = balances.require_find(authorized_account.value, "No balance object found");

        vector<TOKEN_BALANCE> assets = {};
        TOKEN_BALANCE new_balance = {};
        new_balance.quantity = maximum_supply;
        new_balance.contract = contract;

        assets.push_back(new_balance);

        withdraw_balances(authorized_account, assets);
    }
}

ACTION rwax::erasetoken(
    name authorized_account,
    TOKEN_BALANCE token
) {
    require_auth(authorized_account);

    auto token_itr = tokens.require_find(token.quantity.symbol.code().raw(), "Token not found");

    check(token_itr->authorized_account == authorized_account, "No authorized to erase Token");

    for (TEMPLATE tmpl : token_itr->templates) {
        auto template_pool_itr = templpools.find(tmpl.template_id);
        if (template_pool_itr != templpools.end()) {
            templpools.erase(template_pool_itr);
        }
    }

    assetpools_t asset_pools = get_assetpool(token.quantity.symbol.code().raw());

    auto apool_itr = asset_pools.begin();

    vector<uint64_t> asset_ids = {};
    while (apool_itr != asset_pools.end()) {
        asset_ids.push_back(apool_itr->asset_id);
        asset_pools.erase(apool_itr);
        apool_itr = asset_pools.begin();
    }

    action(
        permission_level{get_self(), name("active")},
        token.contract,
        name("transfer"),
        make_tuple(
            get_self(),
            authorized_account,
            token_itr->maximum_supply - token_itr->issued_supply,
            string("RWAX: Erasing Token")
        )
    ).send();

    if (asset_ids.size() > 0) {
        action(
            permission_level{get_self(), name("active")},
            name("atomicassets"),
            name("transfer"),
            make_tuple(
                get_self(),
                authorized_account,
                asset_ids,
                string("RWAX: Erasing Token")
            )
        ).send();
    }

    tokens.erase(token_itr);
}

ACTION rwax::tokenizenfts(
    name user,
    vector<uint64_t> asset_ids
) {
    require_auth(user);

    auto transfer_itr = transfers.require_find(user.value, "No assets found");

    vector<uint64_t> new_assets = transfer_itr->assets;

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

void rwax::check_collection_auth(name collection_name, name authorized_account) {
    require_auth(authorized_account);

    auto collection_itr = collections.require_find(collection_name.value,
        "No collection with this name exists");

    check(std::find(
        collection_itr->authorized_accounts.begin(),
        collection_itr->authorized_accounts.end(),
        authorized_account
        ) != collection_itr->authorized_accounts.end(),
        "Account is not authorized");
}

bool rwax::is_token_supported(
    name token_contract,
    symbol token_symbol
) {
    tokens_t tokens = get_tokens(token_contract);

    auto token_itr = tokens.find(token_symbol.code().raw());

    if (token_itr == tokens.end()) {
        check(false, "Token not supported");
    }

    return true;
}

name rwax::find_asset_pool(
    asset token
) {
    config_s current_config = config.get();
    bool found = false;
    for (name pool : current_config.stake_pools) {
        stakepools_t stakepools = get_stakepools(pool);
        auto stake_itr = stakepools.begin();
        while (stake_itr != stakepools.end() && !found) {
            if (stake_itr->stake_token == token.symbol) {
                return pool;
            }

            ++stake_itr;
        }
    }

    return get_self();
}

float rwax::get_maximum_factor(vector<TRAITFACTOR> trait_factors) {
    float maximum_factor = 1;
    for (TRAITFACTOR factor : trait_factors) {
        maximum_factor *= factor.max_factor;
    }

    return maximum_factor;
}

asset rwax::calculate_issued_tokens(
    uint64_t asset_id, 
    int32_t template_id
) {
    assets_t own_assets = get_assets(get_self());
    auto asset_itr = own_assets.find(asset_id);

    templates_t templates = get_templates(asset_itr->collection_name);
    schemas_t schemas = get_schemas(asset_itr->collection_name);

    auto template_itr = templates.find(template_id);

    auto schema_itr = schemas.find(template_itr->schema_name.value);

    auto template_pool_itr = templpools.find(template_itr->template_id);

    auto token_itr = tokens.find(template_pool_itr->token_share.symbol.code().raw());

    auto trait_itr = traitfactors.find(template_pool_itr->token_share.symbol.code().raw());

    float factor = 1;

    if (trait_itr != traitfactors.end() && trait_itr->trait_factors.size() > 0) {
        float max_factor = get_maximum_factor(trait_itr->trait_factors);
        
        ATTRIBUTE_MAP deserialized_template_data = deserialize(
            template_itr->immutable_serialized_data,
            schema_itr->format
        );

        ATTRIBUTE_MAP deserialized_immutable_data = deserialize(
            asset_itr->immutable_serialized_data,
            schema_itr->format
        );

        ATTRIBUTE_MAP deserialized_mutable_data = deserialize(
            asset_itr->mutable_serialized_data,
            schema_itr->format
        );

        for (TRAITFACTOR trait_factor : trait_itr->trait_factors) {
            ATOMIC_ATTRIBUTE trait;
            bool found = false;
            if (deserialized_template_data.find(trait_factor.trait_name) != deserialized_template_data.end()) {
                trait = deserialized_template_data[trait_factor.trait_name];
                found = true;
            }
            if (deserialized_immutable_data.find(trait_factor.trait_name) != deserialized_immutable_data.end()) {
                trait = deserialized_immutable_data[trait_factor.trait_name];
                found = true;
            }
            if (deserialized_mutable_data.find(trait_factor.trait_name) == deserialized_mutable_data.end()) {
                trait = deserialized_mutable_data[trait_factor.trait_name];
                found = true;
            }

            if (found) {
                string trait_value = std::get<string>(trait);
                if (trait_factor.values.size() > 0) {
                    for (VALUEFACTOR value : trait_factor.values) {
                        if (value.value == trait_value) {
                            factor *= value.factor;
                        }
                    }
                }
            }
        }

        // We have to assume that the max token supply is devided by all redeemable assets when they're maxed out.
        // Therefore the actual factor of any asset has to reduce the amount of issued tokens when it's lower than the maximum factor.
        // Hence we divide the factor by the maximum factor.
        factor /= max_factor;
    }

    asset((template_pool_itr->token_share.amount / template_pool_itr->max_assets_to_tokonize) * factor, template_pool_itr->token_share.symbol);
}

void rwax::tokenize_asset(
    uint64_t asset_id,
    name receiver
) {
    assets_t own_assets = get_assets(get_self());

    auto asset_itr = own_assets.find(asset_id);

    if (asset_itr == own_assets.end()) {
        check(false, ("Asset ID not found: " + to_string(asset_id)).c_str());
    }

    if (asset_itr->template_id <= 0) {
        check(false, ("Invalid Template ID for Asset: " + to_string(asset_id)).c_str());
    }

    templates_t col_templates = get_templates(asset_itr->collection_name);
    auto template_itr = col_templates.find(asset_itr->template_id);

    if (template_itr == col_templates.end()) {
        check(false, ("Template " + to_string(asset_itr->template_id) + " not found").c_str());
    }

    auto template_pool_itr = templpools.find(template_itr->template_id);

    if (template_pool_itr == templpools.end()) {
        check(false, ("Template " + to_string(asset_itr->template_id) + " cannot be tokenized. No Token exists").c_str());
    }

    if (template_pool_itr->currently_tokenized >= template_pool_itr->max_assets_to_tokonize) {
        check(false, ("Template " + to_string(asset_itr->template_id) + " cannot be tokenized. Maximum has been reached.").c_str());
    }

    templpools.modify(template_pool_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized + 1;
    });

    auto token_itr = tokens.find(template_pool_itr->token_share.symbol.code().raw());

    check(token_itr != tokens.end(), "Token not found.");

    asset issued_tokens = calculate_issued_tokens(asset_id, template_itr->template_id);

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply = modified_item.issued_supply + issued_tokens;
    });

    assetpools_t asset_pools = get_assetpool(template_pool_itr->token_share.symbol.code().raw());

    asset_pools.emplace(receiver, [&](auto& new_pool) {
        new_pool.asset_id = asset_id;
    });

    name pool = find_asset_pool(issued_tokens);

    if (pool != get_self()) {
        vector<uint64_t> asset_ids = {};
        asset_ids.push_back(asset_id);

        action(
            permission_level{get_self(), name("active")},
            name("atomicassets"),
            name("transfer"),
            make_tuple(
                get_self(),
                pool,
                asset_ids,
                string("Storing " + to_string(asset_id))
            )
        ).send();
    }

    action(
        permission_level{get_self(), name("active")},
        RWAX_TOKEN_CONTRACT,
        name("transfer"),
        make_tuple(
            get_self(),
            receiver,
            issued_tokens,
            string("Tokenized Asset " + to_string(asset_id))
        )
    ).send();
}

ACTION rwax::init() {
    require_auth(get_self());
    config.get_or_create(get_self(), config_s{});
}

ACTION rwax::addstakepool(
    name pool,
    symbol reward_token,
    symbol stake_token
) {
    require_auth(get_self());

    stakepools_t stakepools = get_stakepools(pool);

    auto pool_itr = stakepools.find(pool.value);
    check(pool_itr == stakepools.end(), "Pool already exists");

    stakepools.emplace(get_self(), [&](auto& _pool) {
        _pool.reward_token = reward_token;
        _pool.stake_token = stake_token;
    });

    config_s current_config = config.get();
    vector<name> current_pools = current_config.stake_pools;
    current_pools.push_back(pool);
    current_config.stake_pools = current_pools;
    config.set(current_config, get_self());
}

ACTION rwax::claim(
    name staker,
    asset token
) {
    require_auth(staker);

    stakes_t token_stakes = get_stakes(token.symbol.code().raw());

    auto stake_itr = token_stakes.require_find(staker.value, "Stake not found");

    for (asset token : stake_itr->rewarded_tokens) {
        name contract = get_token_contract(token.symbol);

        action(
            permission_level{get_self(), name("active")},
            contract,
            name("transfer"),
            make_tuple(
                get_self(),
                staker,
                token,
                string("Claiming staking rewards")
            )
        ).send();
    }

    token_stakes.erase(stake_itr);

    vector<asset> reward_placeholder = {};
    asset reward = asset(0, CORE_SYMBOL);

    token_stakes.modify(stake_itr, same_payer, [&](auto& _item) {
        _item.rewarded_tokens = reward_placeholder;
    });
}

ACTION rwax::redeem(
    name redeemer,
    TOKEN_BALANCE amount
) {
    require_auth(redeemer);

    auto balance_itr = balances.require_find(redeemer.value, "No balance object found");

    check(amount.quantity.amount > 0, "Must redeem positive amount");

    vector<TOKEN_BALANCE> assets = {};

    assets.push_back(amount);

    withdraw_balances(redeemer, assets);

    auto token_itr = tokens.require_find(amount.quantity.symbol.code().raw(), "Token not found");
    asset issued_supply = token_itr->issued_supply;
    uint32_t total_assets_tokenized = 0;
    for (TEMPLATE templ : token_itr->templates) {
        auto templ_itr = templpools.find(templ.template_id);
        total_assets_tokenized += templ_itr->currently_tokenized;
    }

    assetpools_t asset_pools = get_assetpool(amount.quantity.symbol.code().raw());
    auto apool_itr = asset_pools.begin();
    check(apool_itr != asset_pools.end(), "No assets available");

    name asset_pool = find_asset_pool(issued_supply);

    assets_t asset_pool_assets = get_assets(asset_pool);

    auto asset_itr = asset_pool_assets.find(apool_itr->asset_id);

    auto template_pool_itr = templpools.find(asset_itr->template_id);

    templpools.modify(template_pool_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized - 1;
    });

    asset required_amount = calculate_issued_tokens(apool_itr->asset_id, asset_itr->template_id);

    if (required_amount.amount < amount.quantity.amount) {
        check(false, ("Invalid amount. Must transfer exactly " + required_amount.to_string()).c_str());
    }

    asset_pools.erase(apool_itr);

    if (asset_pool == get_self()) {
        vector<uint64_t> asset_ids = {};
        asset_ids.push_back(asset_itr->asset_id);

        action(
            permission_level{get_self(), name("active")},
            name("atomicassets"),
            name("transfer"),
            make_tuple(
                get_self(),
                redeemer,
                asset_ids,
                string("Redeeming from RWAX")
            )
        ).send();
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
        modified_item.issued_supply = modified_item.issued_supply - required_amount;
    });
}

name rwax::get_token_contract(
    symbol token_symbol
) {
    config_s current_config = config.get();

    for (TOKEN supported_token : current_config.supported_tokens) {
        if (supported_token.token_symbol == token_symbol) {
            return supported_token.token_contract;
        }
    }
    return name("eosio.token");
}

ACTION rwax::withdraw(
    vector<TOKEN_BALANCE> tokens,
    name account
) {
    require_auth(account);

    auto& balance_itr = balances.get(account.value, "Balance not Found");

    withdraw_balances(account, tokens);

    for (TOKEN_BALANCE token : tokens) {
        action(
            permission_level{get_self(), name("active")},
            token.contract,
            name("transfer"),
            make_tuple(
                get_self(),
                account,
                token.quantity,
                string("RWAX Balance Withdrawal")
            )
        ).send();
    }
}

void rwax::withdraw_balances(name account, vector<TOKEN_BALANCE> tokens) {
    auto balance_itr = balances.require_find(account.value, "No balance object found");

    vector<TOKEN_BALANCE> new_balances;

    // Check for existing balances that won't get modified first. Add them unchanged
    for (int i = 0; i < balance_itr->assets.size(); i++) {
        bool found = false;
        for (TOKEN_BALANCE token : tokens) {
            if (token.quantity.symbol == balance_itr->assets[i].quantity.symbol) {
                found = true;
            }
        }
        if (!found) {
            new_balances.push_back(balance_itr->assets[i]);
        }
    }

    // Process the balances that get modified.
    for (TOKEN_BALANCE token : tokens) {
        bool found = false;
        for (int i = 0; i < balance_itr->assets.size() && !found; i++) {
            if (balance_itr->assets[i].quantity.symbol == token.quantity.symbol) {
                asset new_amount = balance_itr->assets[i].quantity - token.quantity;
                check(new_amount.amount >= 0 && token.quantity.amount >= 0, "Overdrawn Balance");
                if (new_amount.amount > 0) {
                    TOKEN_BALANCE new_balance = {};
                    new_balance.quantity = new_amount;
                    new_balance.contract = token.contract;
                    new_balances.push_back(new_balance);
                }
                found = true;
            }
        }
        check(found, "Balance not found");
    }

    if (new_balances.size() > 0) {
        balances.modify(balance_itr, same_payer, [&](auto& modified_balance) {
            modified_balance.assets = new_balances;
        });
    } else {
        balances.erase(balance_itr);
    }    
}

void rwax::add_balances(name account, vector<TOKEN_BALANCE> tokens) {
    auto balance_itr = balances.find(account.value);

    if (balance_itr != balances.end()) {
        balances.modify(balance_itr, same_payer, [&](auto& modified_balance) {
            for (TOKEN_BALANCE token : tokens) {
                bool found = false;
                for (int i = 0; i <= modified_balance.assets.size(); i++) {
                    if (modified_balance.assets[i].quantity.symbol == token.quantity.symbol) {
                        int64_t new_amount = modified_balance.assets[i].quantity.amount + token.quantity.amount;
                        modified_balance.assets[i].quantity.set_amount(new_amount);
                        found = true;
                    }
                }
                if (!found) {
                    modified_balance.assets.push_back(token);
                }
            }
        });
    } else {
        balances.emplace(get_self(), [&](auto& _balance) {
            _balance.account = account;
            _balance.assets = tokens;
        });
    }
}

void rwax::receive_transfer(
    name from,
    name to,
    asset quantity,
    string memo
) {
    name contract = get_first_receiver();
    
    if (to != get_self() || (memo != "redeem" && memo != "stake")) {
        return;
    }

    check(is_token_supported(contract, quantity.symbol), "Token not supported");

    if ((memo == "redeem" || memo == "stake") && contract == RWAX_TOKEN_CONTRACT) {
        name account = from;
        vector<TOKEN_BALANCE> tokens = {};
        TOKEN_BALANCE new_balance = {};
        new_balance.quantity = quantity;
        new_balance.contract = contract;
        tokens.push_back(new_balance);
        add_balances(account, tokens);
    }

    if (memo == "reward") {
        stakepools_t stakepools = get_stakepools(from);

        auto pool_itr = stakepools.find(quantity.symbol.code().raw());

        if (pool_itr == stakepools.end()) {
            return;
        }

        stakes_t stakes = get_stakes(pool_itr->stake_token.code().raw());

        auto stake_itr = stakes.begin();

        asset total_staked = stake_itr->amount;
        total_staked.amount = 0;

        while (stake_itr != stakes.end()) {
            total_staked += stake_itr->amount;
            stake_itr++;
        };

        asset rest_amount = quantity;
        while (stake_itr != stakes.end()) {
            stakes.modify(stake_itr, same_payer, [&](auto& modified_stake) {
                asset cut = asset(quantity.amount * ((double)modified_stake.amount.amount / (double)total_staked.amount), quantity.symbol);
                rest_amount -= cut;
                if (rest_amount.amount < 0) {
                    cut += rest_amount;
                }
                vector<asset> new_tokens = modified_stake.rewarded_tokens;
                bool found = false;
                if (modified_stake.rewarded_tokens.size() == 1 && modified_stake.rewarded_tokens[0].symbol == CORE_SYMBOL && modified_stake.rewarded_tokens[0].amount == 0) {
                    modified_stake.rewarded_tokens = {};
                }
                for (int j = 0; j < modified_stake.rewarded_tokens.size() && !found; ++j) { 
                    if (modified_stake.rewarded_tokens[j].symbol == cut.symbol) {
                        found = true;
                        modified_stake.rewarded_tokens[j].amount += cut.amount;
                    }
                }
                if (!found && cut.amount > 0) {
                    modified_stake.rewarded_tokens.push_back(cut);
                }
            });
            stake_itr++;
        }
    }
}

void rwax::receive_asset_transfer(
    name from,
    name to,
    vector<uint64_t> asset_ids,
    string memo
) {
    if (to != get_self()) {
        return;
    }

    check(memo.find("deposit") == 0, "Invalid Memo.");

    vector<uint64_t> assets_to_add = {};
    for (uint64_t asset_id : asset_ids) {
        assets_to_add.push_back(asset_id);
    }

    auto transfer_itr = transfers.find(from.value);
    if (transfer_itr == transfers.end()) {
        transfers.emplace(get_self(), [&](auto& _transfer) {
            _transfer.user = from;
            _transfer.assets = assets_to_add;
        });
    } else {
        transfers.modify(transfer_itr, get_self(), [&](auto& modified_transfer) {
            vector<uint64_t> A = modified_transfer.assets;
            struct {
                bool operator()(uint64_t a, uint64_t b) const { return a < b; }
            } sorter;
            std::sort(assets_to_add.begin(), assets_to_add.end(), sorter);
            std::sort(A.begin(), A.end(), sorter);
            vector<uint64_t> AB(assets_to_add.size() + A.size());
            vector<uint64_t>::iterator it = set_union(A.begin(), A.end(), assets_to_add.begin(), assets_to_add.end(), AB.begin());
            AB.resize(it-AB.begin());
            modified_transfer.assets = AB;
        });
    }
}
