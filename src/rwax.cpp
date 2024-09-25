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
        check(templ.max_assets_to_tokenize > 0, "Need to provide a maximum number of assets to tokenize");
        total_assets_to_tokenize += templ.max_assets_to_tokenize;
    }

    config_s current_config = config.get();
    asset fees = current_config.tokenize_fees;
    if (fees.amount > 0) {
        vector<TOKEN_BALANCE> fee_assets = {};
        TOKEN_BALANCE fee_asset = {};
        fee_asset.quantity = fees;
        fee_asset.contract = RWAX_TOKEN_CONTRACT;
        fee_assets.push_back(fee_asset);
        withdraw_balances(authorized_account, fee_assets);
        add_balances(get_self(), fee_assets);
    }
    
    for (TEMPLATE templ : templates) {
        int32_t template_id = templ.template_id;
        auto template_itr = collection_templates.find(template_id);

        if (template_itr == collection_templates.end()) {
            check(false, ("No template with this ID exists: " + to_string(template_id)).c_str());
        }

        if (template_itr->max_supply > 0) {
            check(templ.max_assets_to_tokenize <= template_itr->max_supply,
                "Templates actual supply is less than given max supply");
        }

        auto template_pool_itr = templpools.find(template_id);

        if (template_pool_itr != templpools.end()) {
            check(false, ("Template " + to_string(template_id) + " has already been tokenized").c_str());
        }

        asset template_supply = asset(maximum_supply.amount * (templ.max_assets_to_tokenize / total_assets_to_tokenize), maximum_supply.symbol);

        templpools.emplace(authorized_account, [&](auto& new_pool) {
            new_pool.template_id = template_id;
            new_pool.max_assets_to_tokenize = templ.max_assets_to_tokenize;
            new_pool.currently_tokenized = 0;
            new_pool.token = maximum_supply;
            new_pool.contract = contract;
        });
    }

    tokens_t tokens = get_tokens(contract);

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
        traitfactors_t traitfactors = get_traitfactors(contract);
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
    name contract,
    symbol token_symbol
) {
    require_auth(authorized_account);

    tokens_t tokens = get_tokens(contract);

    auto token_itr = tokens.begin();

    check(token_itr->authorized_account == authorized_account, "Not authorized to erase Token");

    for (TEMPLATE tmpl : token_itr->templates) {
        auto template_pool_itr = templpools.find(tmpl.template_id);
        if (template_pool_itr != templpools.end()) {
            templpools.erase(template_pool_itr);
        }
    }

    check(false, "Hier?");

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
    if (token_contract == RWAX_TOKEN_CONTRACT && token_symbol == FEE_SYMBOL) {
        return true;
    }

    tokens_t tokens = get_tokens(token_contract);

    auto token_itr = tokens.find(token_symbol.code().raw());

    if (token_itr == tokens.end()) {
        check(false, "Token not supported");
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

    tokens_t tokens = get_tokens(template_pool_itr->contract);

    auto token_itr = tokens.find(template_pool_itr->token.symbol.code().raw());

    traitfactors_t traitfactors = get_traitfactors(template_pool_itr->contract);

    auto trait_itr = traitfactors.find(template_pool_itr->token.symbol.code().raw());

    asset total_supply = token_itr->maximum_supply;
    asset total_trait_factor_token_supply = asset(0, total_supply.symbol);
    asset trait_factor_tokens = asset(0, total_supply.symbol);

    if (trait_itr != traitfactors.end() && trait_itr->trait_factors.size() > 0) {
        for (TRAITFACTOR trait_factor : trait_itr->trait_factors) {
            //total_trait_factor_token_supply += trait_factor.token_share;
        }
        
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

        int cnt = 0;
        for (TRAITFACTOR trait_factor : trait_itr->trait_factors) {
            float factor = 1;

            float avg_factor = 0;//trait_factor.avg_factor;
            asset trait_factor_share = {};//trait_factor.token_share;

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
            if (deserialized_mutable_data.find(trait_factor.trait_name) != deserialized_mutable_data.end()) {
                trait = deserialized_mutable_data[trait_factor.trait_name];
                found = true;
            }

            if (found) {
                string trait_value = std::get<string>(trait);

                if (trait_factor.values.size() > 0) {
                    for (VALUEFACTOR value : trait_factor.values) {
                        if (value.value == trait_value) {
                            factor = value.factor;
                        }
                    }
                } else {
                    double value = 0;
                    for (FORMAT format : schema_itr->format) {
                        if (format.name == trait_factor.trait_name) {
                            if (format.type == "int8_t") {
                                value = std::get<int8_t>(trait);
                            } else if (format.type == "int16_t") {
                                value = std::get<int16_t>(trait);
                            } else if (format.type == "int32_t") {
                                value = std::get<int32_t>(trait);
                            } else if (format.type == "int64_t") {
                                value = std::get<int64_t>(trait);
                            } else if (format.type == "uint8_t") {
                                value = std::get<uint8_t>(trait);
                            } else if (format.type == "uint16_t") {
                                value = std::get<uint16_t>(trait);
                            } else if (format.type == "uint32_t") {
                                value = std::get<uint32_t>(trait);
                            } else if (format.type == "uint64") {
                                value = std::get<uint64_t>(trait);
                            } else if (format.type == "float") {
                                value = std::get<float>(trait);
                            } else if (format.type == "double") {
                                value = std::get<double>(trait);
                            }
                        }
                    }
                    factor = ((trait_factor.max_factor - trait_factor.min_factor) / (trait_factor.max_value - trait_factor.min_value)) * (value - trait_factor.min_value) + trait_factor.min_factor;
                }
            }

            asset trait_factor_tokens = asset(
                0,
                //(trait_factor.token_share.amount / token_itr->max_assets_to_tokenize) * (factor / avg_factor), 
                token_itr->maximum_supply.symbol
            );
        }
    }

    return asset((total_supply.amount - total_trait_factor_token_supply.amount) / 
    1
        //token_itr->max_assets_to_tokenize
        , total_supply.symbol) + total_trait_factor_token_supply;
}

void rwax::tokenize_asset(
    uint64_t asset_id,
    name tokenizer
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

    if (template_pool_itr->currently_tokenized >= template_pool_itr->max_assets_to_tokenize) {
        check(false, ("Template " + to_string(asset_itr->template_id) + " cannot be tokenized. Maximum has been reached.").c_str());
    }

    templpools.modify(template_pool_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized + 1;
    });

    tokens_t tokens = get_tokens(template_pool_itr->contract);

    auto token_itr = tokens.find(template_pool_itr->token.symbol.code().raw());

    check(token_itr != tokens.end(), "Token not found.");

    asset issued_tokens = calculate_issued_tokens(asset_id, template_itr->template_id);
    check((token_itr->issued_supply + issued_tokens).amount <= token_itr->maximum_supply.amount, 
        "Tokenization exceeds Token Supply. Wait until more assets have been redeemed or contact collection");

    tokens.modify(token_itr, same_payer, [&](auto& modified_item) {
        modified_item.issued_supply = modified_item.issued_supply + issued_tokens;
    });

    assetpools_t asset_pools = get_assetpool(token_itr->maximum_supply.symbol.code().raw());

    auto itr = asset_pools.find(asset_id);
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
    config.get_or_create(get_self(), config_s{});
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

ACTION rwax::redeem(
    name redeemer,
    TOKEN_BALANCE amount,
    uint64_t asset_id
) {
    require_auth(redeemer);

    auto balance_itr = balances.require_find(redeemer.value, "No balance object found");

    config_s current_config = config.get();
    asset fees = current_config.redeem_fees;
    if (fees.amount > 0) {
        vector<TOKEN_BALANCE> fee_assets = {};
        TOKEN_BALANCE fee_asset = {};
        fee_asset.quantity = fees;
        fee_asset.contract = RWAX_TOKEN_CONTRACT;
        fee_assets.push_back(fee_asset);
        withdraw_balances(redeemer, fee_assets);
        add_balances(get_self(), fee_assets);
    }

    check(amount.quantity.amount > 0, "Must redeem positive amount");

    vector<TOKEN_BALANCE> assets = {};

    assets.push_back(amount);

    withdraw_balances(redeemer, assets);

    tokens_t tokens = get_tokens(amount.contract);

    auto token_itr = tokens.require_find(amount.quantity.symbol.code().raw(), "Token not found");
    asset issued_supply = token_itr->issued_supply;
    uint32_t total_assets_tokenized = 0;
    for (TEMPLATE templ : token_itr->templates) {
        auto templ_itr = templpools.find(templ.template_id);
        total_assets_tokenized += templ_itr->currently_tokenized;
    }

    assetpools_t asset_pools = get_assetpool(amount.quantity.symbol.code().raw());

    bool matched = false;

    auto apool_itr = asset_pools.require_find(asset_id, "Asset not found");

    assets_t asset_pool_assets = get_assets(get_self());

    auto asset_itr = asset_pool_assets.find(apool_itr->asset_id);

    auto template_pool_itr = templpools.find(asset_itr->template_id);

    templpools.modify(template_pool_itr, same_payer, [&](auto& modified_item) {
        modified_item.currently_tokenized = modified_item.currently_tokenized - 1;
    });

    asset required_amount = calculate_issued_tokens(apool_itr->asset_id, asset_itr->template_id);

    check(required_amount.amount == amount.quantity.amount, ("Must transfer exactly " + required_amount.to_string()).c_str());

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
        modified_item.issued_supply = modified_item.issued_supply - required_amount;
    });
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
    
    if (to != get_self() || (memo != "redeem" && memo != "payfee")) {
        return;
    }

    check(is_token_supported(contract, quantity.symbol), "Token not supported");

    if (memo == "redeem" || memo == "payfee") {
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
