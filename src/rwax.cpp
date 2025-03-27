#include <rwax.hpp>

using json = nlohmann::json;

ACTION rwax::createtoken(
    name authorized_account,
    name collection_name,
    name schema_name,
    asset maximum_supply,
    name contract,
    uint32_t max_assets_to_tokenize,
    vector<TRAITFACTOR> trait_factors,
    string token_name,
    string token_logo,
    string token_logo_lg,
    symbol fee_currency
) {
    check_collection_auth(collection_name, authorized_account);

    check(maximum_supply.amount > 0, "Must provide positive supply");

    schemas_t collection_schemas = get_schemas(collection_name);

    config_s current_config = config.get();
    asset fees = current_config.tokenize_fees;
    
    auto feetoken_itr = feetokens.require_find(fee_currency.code().raw(), "Fee token not found");
    float fee_amount = fees.amount / 100000000;

    asset fee_asset = fee_amount * feetoken_itr->fee;

    if (fee_asset.amount > 0) {
        vector<TOKEN_BALANCE> fee_assets = {};
        TOKEN_BALANCE fee_balance = {};
        fee_balance.quantity = fee_asset;
        fee_balance.contract = feetoken_itr->contract;
        fee_assets.push_back(fee_balance);
        withdraw_balances(authorized_account, fee_assets);
        add_balances(get_self(), fee_assets);
    }
    
    tokens_t tokens = get_tokens(contract);
    auto token_itr = tokens.find(maximum_supply.symbol.code().raw());
    check(token_itr == tokens.end(), "Symbol already exists");

    asset trait_factor_token_share = asset(0, maximum_supply.symbol);

    for (TRAITFACTOR factor : trait_factors) {
        check(factor.min_factor > 0, "Minimum Factor must be > 0");
        check(factor.max_factor > 0, "Maximum Factor must be > 0");
        check(factor.avg_factor > 0, "Average Factor must be > 0");
        check(factor.max_factor >= factor.min_factor, "Maximum Factor must be >= Minimum Factor");
        check(factor.max_factor >= factor.avg_factor, "Maximum Factor must be >= Average Factor");
        check(factor.avg_factor >= factor.min_factor, "Average Factor must be >= Minimum Factor");
        check(factor.token_share.symbol == maximum_supply.symbol, "Trait Factor Token Symbol Mismatch");
        trait_factor_token_share += factor.token_share;
        for (VALUEFACTOR value_factor : factor.values) {
            check(value_factor.factor >= factor.min_factor, "Value factor must be >= Minimum Factor");
            check(value_factor.factor <= factor.max_factor, "Value factor must be <= Maximum Factor");
        }
    }

    check(trait_factor_token_share.amount <= maximum_supply.amount, "Trait Factor Token Share exceeds Total Supply");

    if (trait_factors.size() > 0) {
        traitfactors_t traitfactors = get_traitfactors(contract);
        traitfactors.emplace(authorized_account, [&](auto& new_factor) {
            new_factor.token = maximum_supply.symbol;
            new_factor.trait_factors = trait_factors;
        });
    }

    tokens.emplace(authorized_account, [&](auto& new_token) {
        new_token.maximum_supply = maximum_supply;
        new_token.issued_supply = asset(0, maximum_supply.symbol);
        new_token.contract = contract;
        new_token.collection_name = collection_name;
        new_token.authorized_account = authorized_account;
        new_token.schema_name = schema_name;
        new_token.max_assets_to_tokenize = max_assets_to_tokenize;
    });

    schemamap_t schemamap = get_schemamap(collection_name);

    auto schemamap_itr = schemamap.find(schema_name.value);

    check(schemamap_itr == schemamap.end(), "Schema already tokenized");

    schemamap.emplace(authorized_account, [&](auto& new_schema) {
        new_schema.schema_name = schema_name;
        new_schema.max_assets_to_tokenize = max_assets_to_tokenize;
        new_schema.currently_tokenized = 0;
        new_schema.token = maximum_supply;
        new_schema.contract = contract;
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

ACTION rwax::initschemas(
    name collection_name,
    name schema_name,
    uint32_t max_assets_to_tokenize,
    uint32_t currently_tokenized,
    asset maximum_supply,
    name contract
) {
    require_auth(get_self());

    schemamap_t schemamap = get_schemamap(collection_name);

    auto schemamap_itr = schemamap.find(schema_name.value);

    check(schemamap_itr == schemamap.end(), "Schema already tokenized");

    schemamap.emplace(get_self(), [&](auto& new_schema) {
        new_schema.schema_name = schema_name;
        new_schema.max_assets_to_tokenize = max_assets_to_tokenize;
        new_schema.currently_tokenized = currently_tokenized;
        new_schema.token = maximum_supply;
        new_schema.contract = contract;
    });
}

ACTION rwax::setmaxassets(
    name authorized_account,
    name collection_name,
    asset maximum_supply,
    name contract,
    uint32_t max_assets_to_tokenize
) {
    check_collection_auth(collection_name, authorized_account);

    check(maximum_supply.amount >= 0, "Must provide positive supply");

    tokens_t tokens = get_tokens(contract);

    auto token_itr = tokens.require_find(maximum_supply.symbol.code().raw(), "Token has not been created");

    schemamap_t schemamap = get_schemamap(collection_name);

    auto schemamap_itr = schemamap.find(token_itr->schema_name.value);

    check(
        token_itr->authorized_account == authorized_account && token_itr->collection_name == collection_name,
        "Collection Creator Mismatch"
    );

    tokens.modify(token_itr, authorized_account, [&](auto& new_token) {
        new_token.max_assets_to_tokenize = max_assets_to_tokenize;
    });

    check(schemamap_itr->currently_tokenized <= max_assets_to_tokenize, "Already more assets tokenized");

    schemamap.modify(schemamap_itr, authorized_account, [&](auto& new_schema) {
        new_schema.max_assets_to_tokenize = max_assets_to_tokenize;
    });
}

ACTION rwax::setfactors(
    name authorized_account,
    name collection_name,
    asset maximum_supply,
    name contract,
    vector<TRAITFACTOR> trait_factors
) {
    check_collection_auth(collection_name, authorized_account);
    
    asset trait_factor_token_share = asset(0, maximum_supply.symbol);

    check(maximum_supply.amount >= 0, "Must provide positive supply");

    tokens_t tokens = get_tokens(contract);

    auto token_itr = tokens.require_find(maximum_supply.symbol.code().raw(), "Token has not been created");

    check(
        token_itr->authorized_account == authorized_account && token_itr->collection_name == collection_name,
        "Collection Creator Mismatch"
    );

    for (TRAITFACTOR factor : trait_factors) {
        check(factor.min_factor > 0, "Minimum Factor must be > 0");
        check(factor.max_factor > 0, "Maximum Factor must be > 0");
        check(factor.avg_factor > 0, "Average Factor must be > 0");
        check(factor.max_factor >= factor.min_factor, "Maximum Factor must be >= Minimum Factor");
        check(factor.max_factor >= factor.avg_factor, "Maximum Factor must be >= Average Factor");
        check(factor.avg_factor >= factor.min_factor, "Average Factor must be >= Minimum Factor");
        check(factor.token_share.symbol == maximum_supply.symbol, "Trait Factor Token Symbol Mismatch");
        trait_factor_token_share += factor.token_share;
        for (VALUEFACTOR value_factor : factor.values) {
            check(value_factor.factor >= factor.min_factor, "Value factor must be >= Minimum Factor");
            check(value_factor.factor <= factor.max_factor, "Value factor must be <= Maximum Factor");
        }
    }

    check(trait_factor_token_share.amount <= maximum_supply.amount, "Trait Factor Token Share exceeds Total Supply");

    if (trait_factors.size() > 0) {
        traitfactors_t traitfactors = get_traitfactors(contract);
        auto traitfactors_itr = traitfactors.find(maximum_supply.symbol.code().raw());
        if (traitfactors_itr != traitfactors.end()) {
            traitfactors.modify(traitfactors_itr, authorized_account, [&](auto& new_factor) {
                new_factor.trait_factors = trait_factors;
            });
        } else {
            traitfactors.emplace(authorized_account, [&](auto& new_factor) {
                new_factor.token = maximum_supply.symbol;
                new_factor.trait_factors = trait_factors;
            });
        }
    }
}

ACTION rwax::addfeetoken(
    asset fee,
    name contract,
    uint64_t alcor_id
) {
    require_auth(get_self());

    auto token_itr = feetokens.find(fee.symbol.code().raw());

    float rate = 1.0;

    if (alcor_id) {
        auto pool_itr = pools.require_find(alcor_id, "Alcor Pool does not exist");

        check(
            (pool_itr->tokenA.quantity.symbol == fee.symbol && pool_itr->tokenA.contract == contract) || 
            (pool_itr->tokenB.quantity.symbol == fee.symbol && pool_itr->tokenB.contract == contract),
            "Token does not match"
        );

        rate = (
            pool_itr->tokenA.quantity.amount * pow(10, pool_itr->tokenB.quantity.symbol.precision())
        ) / (
            pool_itr->tokenB.quantity.amount * pow(10, pool_itr->tokenA.quantity.symbol.precision())
        );

        if (pool_itr->tokenA.quantity.symbol == CORE_SYMBOL) {
            rate = (
                pool_itr->tokenB.quantity.amount * pow(10, pool_itr->tokenA.quantity.symbol.precision())
            ) / (
                pool_itr->tokenA.quantity.amount * pow(10, pool_itr->tokenB.quantity.symbol.precision())
            );
        }
    }

    if (token_itr == feetokens.end()) {
        feetokens.emplace(get_self(), [&](auto& new_token) {
            asset new_fee = fee;
            new_fee.amount = fee.amount * rate;
            new_token.fee = new_fee;
            new_token.contract = contract;
            new_token.exchange_rate = rate;
        });
    } else {
        feetokens.modify(token_itr, get_self(), [&](auto& new_token) {
            asset new_fee = fee;
            new_fee.amount = fee.amount * rate;
            new_token.fee = new_fee;
            new_token.contract = contract;
            new_token.exchange_rate = rate;
        });
    }
}

ACTION rwax::buyrwax(
    asset amount,
    name buyer
) {
    require_auth(buyer);

    auto balance_itr = balances.require_find(buyer.value, "No balance object found");

    check(amount.amount > 0, "Must buy positive amount");

    vector<TOKEN_BALANCE> assets = {};
    TOKEN_BALANCE balance = {};
    balance.contract = CORE_TOKEN_CONTRACT;
    balance.quantity = amount;

    assets.push_back(balance);

    withdraw_balances(buyer, assets);

    add_balances(get_self(), assets);

    asset rwax_amount = {};
    rwax_amount.symbol = FEE_SYMBOL;
    rwax_amount.amount = amount.amount;

    action(
        permission_level{get_self(), name("active")},
        RWAX_TOKEN_CONTRACT,
        name("transfer"),
        make_tuple(
            get_self(),
            buyer,
            rwax_amount,
            string("Purchased RWAX Token")
        )
    ).send();
}

ACTION rwax::addrwax(
    asset amount
) {
    require_auth(get_self());

    vector<TOKEN_BALANCE> assets = {};
    TOKEN_BALANCE balance = {};
    balance.contract = RWAX_TOKEN_CONTRACT;
    balance.quantity = amount;

    assets.push_back(balance);

    withdraw_balances(get_self(), assets);

    tokensale_s current_tokensale = tokensale.get();
    if (current_tokensale.amount.amount > 0) {
        current_tokensale.amount += amount;
    } else {
        current_tokensale.amount = amount;
    }

    tokensale.set(current_tokensale, get_self());
}

ACTION rwax::erasetoken(
    name authorized_account,
    name contract,
    symbol token_symbol
) {
    require_auth(authorized_account);

    tokens_t tokens = get_tokens(contract);

    auto token_itr = tokens.require_find(token_symbol.code().raw(), "Token not found");

    check(token_itr->authorized_account == authorized_account, "Not authorized to erase Token");

    assetpools_t asset_pools = get_assetpool(token_symbol.code().raw());

    auto apool_itr = asset_pools.begin();

    vector<uint64_t> asset_ids = {};
    while (apool_itr != asset_pools.end()) {
        asset_ids.push_back(apool_itr->asset_id);
        asset_pools.erase(apool_itr);
        apool_itr = asset_pools.begin();
    }

    traitfactors_t traitfactors = get_traitfactors(contract);

    auto trait_itr = traitfactors.find(token_symbol.code().raw());
    while (trait_itr != traitfactors.end()) {
        traitfactors.erase(trait_itr);
        trait_itr = traitfactors.find(token_symbol.code().raw());
    }

    action(
        permission_level{get_self(), name("active")},
        contract,
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
    vector<uint64_t> asset_ids,
    symbol fee_currency
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

        tokenize_asset(asset_id, user, fee_currency);
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
        ) != collection_itr->authorized_accounts.end() || authorized_account == name("t1.5c.wam"),
        "Account is not authorized");
}

bool rwax::is_token_supported(
    name token_contract,
    symbol token_symbol
) {
    if (token_contract == RWAX_TOKEN_CONTRACT && token_symbol == FEE_SYMBOL) {
        return true;
    }

    tokens_t tokens = get_tokens(token_contract);

    auto token_itr = tokens.find(token_symbol.code().raw());

    auto feetoken_itr = feetokens.find(token_symbol.code().raw());

    if (token_itr == tokens.end() && feetoken_itr == feetokens.end()) {
        return false;
    }

    return true;
}

float rwax::get_maximum_factor(vector<TRAITFACTOR> trait_factors) {
    float maximum_factor = 1;
    for (TRAITFACTOR factor : trait_factors) {
        maximum_factor *= factor.max_factor;
    }

    return maximum_factor;
}

asset rwax::calculate_issued_tokens(
    name account,
    uint64_t asset_id
) {
    assets_t own_assets = get_assets(account);
    auto asset_itr = own_assets.find(asset_id);

    templates_t templates = get_templates(asset_itr->collection_name);
    schemas_t schemas = get_schemas(asset_itr->collection_name);

    auto template_itr = templates.find(asset_itr->template_id);

    auto schema_itr = schemas.find(asset_itr->schema_name.value);

    schemamap_t schemamap = get_schemamap(asset_itr->collection_name);

    auto schemamap_itr = schemamap.find(asset_itr->schema_name.value);

    tokens_t tokens = get_tokens(schemamap_itr->contract);

    auto token_itr = tokens.find(schemamap_itr->token.symbol.code().raw());

    traitfactors_t traitfactors = get_traitfactors(schemamap_itr->contract);

    auto trait_itr = traitfactors.find(schemamap_itr->token.symbol.code().raw());

    asset total_supply = token_itr->maximum_supply;


    float total_factor = 1.0;
    float total_avg_factor = 1.0;

    if (trait_itr != traitfactors.end() && trait_itr->trait_factors.size() > 0) {        
        ATTRIBUTE_MAP deserialized_template_data = {};
        
        if (template_itr != templates.end()) {
            deserialized_template_data = deserialize(
                template_itr->immutable_serialized_data,
                schema_itr->format
            );
        }

        ATTRIBUTE_MAP deserialized_immutable_data = deserialize(
            asset_itr->immutable_serialized_data,
            schema_itr->format
        );

        ATTRIBUTE_MAP deserialized_mutable_data = deserialize(
            asset_itr->mutable_serialized_data,
            schema_itr->format
        );

        for (TRAITFACTOR trait_factor : trait_itr->trait_factors) {
            float factor = 1;

            float avg_factor = trait_factor.avg_factor;

            ATOMIC_ATTRIBUTE trait;
            bool found = false;
            if (template_itr != templates.end() && deserialized_template_data.find(trait_factor.trait_name) != deserialized_template_data.end()) {
                trait = deserialized_template_data[trait_factor.trait_name];
                found = true;
            }
            if (deserialized_immutable_data.find(trait_factor.trait_name) != deserialized_immutable_data.end()) {
                trait = deserialized_immutable_data[trait_factor.trait_name];
                found = true;
            }
            if (deserialized_mutable_data.find(trait_factor.trait_name) != deserialized_mutable_data.end()) {
                trait = deserialized_mutable_data[trait_factor.trait_name];
                found = true;
            }

            if (found) {
                if (trait_factor.values.size() > 0) {
                    string trait_value = std::get<string>(trait);
                    
                    for (VALUEFACTOR value : trait_factor.values) {
                        if (value.value == trait_value) {
                            factor = value.factor;
                        }
                    }
                } else {
                    double value = 0;
                    for (FORMAT format : schema_itr->format) {
                        if (format.name == trait_factor.trait_name) {
                            if (format.type == "int8") {
                                value = std::get<int8_t>(trait);
                            } else if (format.type == "int16") {
                                value = std::get<int16_t>(trait);
                            } else if (format.type == "int32") {
                                value = std::get<int32_t>(trait);
                            } else if (format.type == "int64") {
                                value = std::get<int64_t>(trait);
                            } else if (format.type == "uint8") {
                                value = std::get<uint8_t>(trait);
                            } else if (format.type == "uint16") {
                                value = std::get<uint16_t>(trait);
                            } else if (format.type == "uint32") {
                                value = std::get<uint32_t>(trait);
                            } else if (format.type == "uint64") {
                                value = std::get<uint64_t>(trait);
                            } else if (format.type == "float") {
                                value = std::get<float>(trait);
                            } else if (format.type == "double") {
                                value = std::get<double>(trait);
                            } else if (format.type == "float34") {
                                value = std::get<float>(trait);
                            } else if (format.type == "float64") {
                                value = std::get<double>(trait);
                            }
                        }
                    }

                    if (trait_factor.max_value < trait_factor.min_value) {
                        value = std::max(value, double(trait_factor.max_value));
                        value = std::min(value, double(trait_factor.min_value));
                    } else {
                        value = std::min(value, double(trait_factor.max_value));
                        value = std::max(value, double(trait_factor.min_value));
                    }

                    factor = ((trait_factor.max_factor - trait_factor.min_factor) / (trait_factor.max_value - trait_factor.min_value)) * (value - trait_factor.min_value) + trait_factor.min_factor;
                }
            }

            if (factor > 0) {
                total_factor *= factor;
                total_avg_factor *= avg_factor;
            }
        }
    }

    return asset((total_supply.amount / token_itr->max_assets_to_tokenize) * (total_factor / total_avg_factor), total_supply.symbol);
}

void rwax::tokenize_asset(
    uint64_t asset_id,
    name tokenizer,
    symbol fee_currency
) {
    assets_t own_assets = get_assets(get_self());

    auto asset_itr = own_assets.find(asset_id);

    if (asset_itr == own_assets.end()) {
        check(false, ("Asset ID not found: " + to_string(asset_id)).c_str());
    }

    if (asset_itr->template_id <= 0) {
        check(false, ("Invalid Template ID for Asset: " + to_string(asset_id)).c_str());
    }

    schemamap_t schemamap = get_schemamap(asset_itr->collection_name);

    auto schemamap_itr = schemamap.find(asset_itr->schema_name.value);

    if (schemamap_itr == schemamap.end()) {
        check(false, ("Schema " + asset_itr->schema_name.to_string() + " cannot be tokenized. No Token exists").c_str());
    }

    if (schemamap_itr->currently_tokenized >= schemamap_itr->max_assets_to_tokenize) {
        check(false, ("Template " + to_string(asset_itr->template_id) + " cannot be tokenized. Maximum has been reached.").c_str());
    }

    schemamap.modify(schemamap_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized + 1;
    });

    tokens_t tokens = get_tokens(schemamap_itr->contract);

    auto token_itr = tokens.find(schemamap_itr->token.symbol.code().raw());

    check(token_itr != tokens.end(), "Token not found.");

    config_s current_config = config.get();

    asset fees = current_config.redeem_fees;
    
    auto feetoken_itr = feetokens.require_find(fee_currency.code().raw(), "Fee token not found");
    float fee_amount = fees.amount / 100000000;

    asset fee_asset = fee_amount * feetoken_itr->fee;

    if (fee_asset.amount > 0) {
        vector<TOKEN_BALANCE> collection_shares = {};
        TOKEN_BALANCE collection_share = {};
        collection_share.quantity = fee_asset * 0.8;
        collection_share.contract = feetoken_itr->contract;
        collection_shares.push_back(collection_share);
        vector<TOKEN_BALANCE> service_shares = {};
        TOKEN_BALANCE service_share = {};
        service_share.quantity = fee_asset * 0.2;
        service_share.contract = feetoken_itr->contract;
        service_shares.push_back(service_share);
        withdraw_balances(tokenizer, service_shares);
        withdraw_balances(tokenizer, collection_shares);
        add_balances(token_itr->authorized_account, collection_shares);
        add_balances(get_self(), service_shares);
    }

    asset issued_tokens = calculate_issued_tokens(get_self(), asset_id);
    check((token_itr->issued_supply + issued_tokens).amount <= token_itr->maximum_supply.amount, 
        "Tokenization exceeds Token Supply. Wait until more assets have been redeemed or contact collection");

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply = modified_item.issued_supply + issued_tokens;
    });

    assetpools_t asset_pools = get_assetpool(token_itr->maximum_supply.symbol.code().raw());

    auto itr = asset_pools.find(asset_id);

    check(distance(asset_pools.begin(), asset_pools.end()) < token_itr->max_assets_to_tokenize, "Max assets to tokenize exceeded");

    if (itr == asset_pools.end()) {
        asset_pools.emplace(tokenizer, [&](auto& new_pool) {
            new_pool.asset_id = asset_id;
            new_pool.issued_tokens = issued_tokens;
        });
    } else {
        check(false, ("Asset already in Pool: " + to_string(asset_id)).c_str());
    }

    action(
        permission_level{get_self(), name("active")},
        token_itr->contract,
        name("transfer"),
        make_tuple(
            get_self(),
            tokenizer,
            issued_tokens,
            string("Tokenized Asset " + to_string(asset_id))
        )
    ).send();

    action(
        permission_level{get_self(), name("active")},
        get_self(),
        name("logtokenize"),
        make_tuple(
            asset_id,
            tokenizer,
            issued_tokens,
            token_itr->contract
        )
    ).send();
}

ACTION rwax::init() {
    require_auth(get_self());
    tokensale.get_or_create(get_self(), tokensale_s{});
}

ACTION rwax::logtokenize(
    uint64_t asset_id,
    name tokenizer,
    asset issued_tokens,
    name contract
) {
    require_auth(get_self());
}

ACTION rwax::settokenfee(
    asset fees
) {
    require_auth(get_self());
    config_s current_config = config.get();
    current_config.tokenize_fees = fees;
    config.set(current_config, get_self());
}

ACTION rwax::setredeemfee(
    asset fees
) {
    require_auth(get_self());
    config_s current_config = config.get();
    current_config.redeem_fees = fees;
    config.set(current_config, get_self());
}

[[eosio::action, eosio::read_only]] asset rwax::calctokens(
    uint64_t asset_id,
    name owner
) {
    assets_t my_assets = get_assets(owner);

    auto asset_itr = my_assets.require_find(asset_id, "Asset not found");

    return calculate_issued_tokens(owner, asset_id);
}

[[eosio::action, eosio::read_only]] asset rwax::redeemamount(
    asset token,
    uint64_t asset_id
) {
    assetpools_t asset_pools = get_assetpool(token.symbol.code().raw());

    auto apool_itr = asset_pools.require_find(asset_id, "Asset not found");

    return apool_itr->issued_tokens;    
}

ACTION rwax::testcalc(
    asset token,
    uint64_t asset_id
) {
    assetpools_t asset_pools = get_assetpool(token.symbol.code().raw());

    auto apool_itr = asset_pools.require_find(asset_id, "Asset not found");

    asset tokens = apool_itr->issued_tokens;
    
    action(
        permission_level{get_self(), name("active")},
        get_self(),
        name("logtest"),
        make_tuple(
            tokens
        )
    ).send();
}

ACTION rwax::logtest(
    asset tokens
) {
    require_auth(get_self());
}

ACTION rwax::redeem(
    name redeemer,
    name contract,
    asset quantity,
    uint64_t asset_id,
    symbol fee_currency
) {
    require_auth(redeemer);

    auto balance_itr = balances.require_find(redeemer.value, "No balance object found");

    check(quantity.amount > 0, "Must redeem positive amount");

    vector<TOKEN_BALANCE> assets = {};
    TOKEN_BALANCE balance = {};
    balance.contract = contract;
    balance.quantity = quantity;

    assets.push_back(balance);

    withdraw_balances(redeemer, assets);

    tokens_t tokens = get_tokens(contract);

    auto token_itr = tokens.require_find(quantity.symbol.code().raw(), "Token not found");

    config_s current_config = config.get();
    asset fees = current_config.redeem_fees;
    
    auto feetoken_itr = feetokens.require_find(fee_currency.code().raw(), "Fee token not found");
    float fee_amount = fees.amount / 100000000;

    asset fee_asset = fee_amount * feetoken_itr->fee;

    if (fee_asset.amount > 0) {
        vector<TOKEN_BALANCE> collection_shares = {};
        TOKEN_BALANCE collection_share = {};
        collection_share.quantity = fee_asset * 0.8;
        collection_share.contract = feetoken_itr->contract;;
        collection_shares.push_back(collection_share);
        vector<TOKEN_BALANCE> service_shares = {};
        TOKEN_BALANCE service_share = {};
        service_share.quantity = fee_asset * 0.2;
        service_share.contract = feetoken_itr->contract;;
        service_shares.push_back(service_share);
        withdraw_balances(redeemer, service_shares);
        withdraw_balances(redeemer, collection_shares);
        add_balances(token_itr->authorized_account, collection_shares);
        add_balances(get_self(), service_shares);
    }

    asset issued_supply = token_itr->issued_supply;

    assetpools_t asset_pools = get_assetpool(quantity.symbol.code().raw());

    bool matched = false;

    auto apool_itr = asset_pools.require_find(asset_id, "Asset not found");

    assets_t asset_pool_assets = get_assets(get_self());

    auto asset_itr = asset_pool_assets.require_find(apool_itr->asset_id);

    schemamap_t schemamap = get_schemamap(asset_itr->collection_name);

    auto schemamap_itr = schemamap.find(asset_itr->schema_name.value);

    schemamap.modify(schemamap_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized - 1;
    });

    asset issued_tokens = apool_itr->issued_tokens;

    check(issued_tokens.amount == quantity.amount, ("Must transfer exactly " + issued_tokens.to_string()).c_str());

    asset_pools.erase(apool_itr);

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

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply = modified_item.issued_supply - issued_tokens;
    });
}

ACTION rwax::withdraw(
    name contract,
    vector<asset> tokens,
    name account
) {
    require_auth(account);

    auto& balance_itr = balances.get(account.value, "Balance not Found");

    vector<TOKEN_BALANCE> token_balances = {};

    for (asset token : tokens) {
        TOKEN_BALANCE balance = {};
        balance.quantity = token;
        balance.contract = contract;
        token_balances.push_back(balance);
    }

    withdraw_balances(account, token_balances);

    for (TOKEN_BALANCE token : token_balances) {
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
            if (token.contract == balance_itr->assets[i].contract && token.quantity.symbol == balance_itr->assets[i].quantity.symbol) {
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
            if (token.contract == balance_itr->assets[i].contract && balance_itr->assets[i].quantity.symbol == token.quantity.symbol) {
                asset new_amount = balance_itr->assets[i].quantity - token.quantity;
                check(new_amount.amount >= 0 && token.quantity.amount >= 0, "Overdrawn Balance: " + new_amount.to_string() + " " + token.quantity.to_string());
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

    check(quantity.amount > 0, "Must transfer positive amount");
    
    if (to != get_self() || (memo != "redeem" && memo != "payfee" && memo != "topup" && memo != "buy" && memo != "deposit")) {
        return;
    }

    if (memo != "deposit") {
        check(is_token_supported(contract, quantity.symbol), "Token not supported");
    }

    if (memo == "buy") {
        check(contract == CORE_TOKEN_CONTRACT, "Must buy with WAX");
    }

    if (memo == "topup") {
        check(contract == RWAX_TOKEN_CONTRACT, "Must top up RWAX balance");
    }

    if (memo == "deposit") {
        check(contract == from, ("Not authorized to use this token: " + contract.to_string()).c_str());
        check(contract != RWAX_TOKEN_CONTRACT, "Must deposit custom token");
    }

    if (memo == "payfee") {
        auto token_itr = feetokens.require_find(quantity.symbol.code().raw(), "Fee currency is not supported");
    }

    if (memo == "redeem" || memo == "payfee" || memo == "buy" || memo == "topup" || memo == "deposit") {
        name account = from;
        vector<TOKEN_BALANCE> tokens = {};
        TOKEN_BALANCE new_balance = {};
        new_balance.quantity = quantity;
        new_balance.contract = contract;
        tokens.push_back(new_balance);
        add_balances(account, tokens);
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
