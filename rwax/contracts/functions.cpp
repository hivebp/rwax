#pragma once

inline eosio::permission_level rwax::active_perm() {
  return eosio::permission_level{ _self, "active"_n };
}

void rwax::add_balances(name account, vector<asset> tokens) {
    auto balance_itr = balances.find(account.value);

    if (balance_itr != balances.end()) {
        balances.modify(balance_itr, same_payer, [&](auto& modified_balance) {
            for (asset token : tokens) {
                bool found = false;
                for (int i = 0; i <= modified_balance.assets.size(); i++) {
                    if (modified_balance.assets[i].symbol == token.symbol) {
                        int64_t new_amount = modified_balance.assets[i].amount + token.amount;
                        modified_balance.assets[i].set_amount(new_amount);
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

assetpools_t rwax::get_assetpool(uint64_t symbolraw) {
    return assetpools_t(_self, symbolraw);
}

assets_t rwax::get_assets(name acc) {
    return assets_t(ATOMICASSETS_CONTRACT, acc.value);
}

float rwax::get_maximum_factor(vector<TRAITFACTOR> trait_factors) {
    float maximum_factor = 1;
    for (TRAITFACTOR factor : trait_factors) {
        maximum_factor *= factor.max_factor;
    }

    return maximum_factor;
}

schemas_t rwax::get_schemas(name collection_name) {
    return schemas_t(ATOMICASSETS_CONTRACT, collection_name.value);
}

stakepools_t rwax::get_stakepools(name pool) {
    return stakepools_t(_self, pool.value);
}

stakes_t rwax::get_stakes(uint64_t symbolraw) {
    return stakes_t(_self, symbolraw);
}

templates_t rwax::get_templates(name collection_name) {
    return templates_t(ATOMICASSETS_CONTRACT, collection_name.value);
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

bool rwax::is_token_supported(
    name token_contract,
    symbol token_symbol
) {
    check(token_contract == RWAX_TOKEN_CONTRACT || token_contract == CORE_TOKEN_CONTRACT, "Unsupported token contract");

    auto token_itr = tokens.find(token_symbol.code().raw());

    if (token_itr == tokens.end()) {
        check(false, "Token not supported");
    }

    return true;
}

void rwax::tokenize_asset(
    uint64_t asset_id,
    name receiver
) {
    assets_t    own_assets  = get_assets(get_self());
    auto        asset_itr   = own_assets.find(asset_id);

    if (asset_itr == own_assets.end()) {
        check(false, ("Asset ID not found: " + to_string(asset_id)).c_str());
    }

    if (asset_itr->template_id <= 0) {
        check(false, ("Invalid Template ID for Asset: " + to_string(asset_id)).c_str());
    }

    templates_t col_templates   = get_templates(asset_itr->collection_name);
    auto        template_itr    = col_templates.find(asset_itr->template_id);

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

    auto    token_itr       = tokens.require_find(template_pool_itr->token_share.symbol.code().raw(), "Token not found.");
    asset   issued_tokens   = calculate_issued_tokens(asset_id, template_itr->template_id);

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply += issued_tokens;
    });

    assetpools_t asset_pools = get_assetpool(template_pool_itr->token_share.symbol.code().raw());

    asset_pools.emplace(receiver, [&](auto& new_pool) {
        new_pool.asset_id = asset_id;
    });

    name pool = find_asset_pool(issued_tokens);

    if (pool != get_self()) {
        transfer_nfts(pool, {asset_id}, string("Storing " + to_string(asset_id)));
    }

    transfer_tokens(receiver, issued_tokens, RWAX_TOKEN_CONTRACT, string("Tokenized Asset " + to_string(asset_id)));
}

void rwax::transfer_nfts(const name& user, const vector<uint64_t>& asset_ids, const std::string& memo){
  action(
    active_perm(),
    ATOMICASSETS_CONTRACT,
    "transfer"_n,
    std::tuple{ _self, user, asset_ids, memo }
  ).send();
}

void rwax::transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo){
  action(active_perm(), contract, "transfer"_n, std::tuple{ get_self(), user, amount_to_send, memo}).send();
}


void rwax::withdraw_balances(name account, vector<asset> tokens) {
    auto balance_itr = balances.require_find(account.value, "No balance object found");

    vector<asset> new_balances;

    // Check for existing balances that won't get modified first. Add them unchanged
    for (asset a : balance_itr->assets) {
        bool found = false;
        for (asset token : tokens) {
            if (token.symbol == a.symbol) {
                found = true;
            }
        }
        if (!found) {
            new_balances.push_back(a);
        }
    }

    // Process the balances that get modified.
    for (asset token : tokens) {
        bool found = false;
        for (asset a : balance_itr->assets) {
            if (a.symbol == token.symbol) {
                asset new_amount = a - token;
                check(new_amount.amount >= 0 && token.amount >= 0, "Overdrawn Balance");
                if (new_amount.amount > 0) {
                    new_balances.push_back(new_amount);
                }
                found = true;
                break;
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