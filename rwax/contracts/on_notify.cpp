#pragma once

void rwax::receive_any_transfer(
    name    from,
    name    to,
    asset   quantity,
    string  memo
) {
    name contract = get_first_receiver();
    
    if (from == _self || to != _self || quantity.amount == 0) return;

    check(quantity.amount > 0, "Quantity must be positive");
    check(is_token_supported(contract, quantity.symbol), "Token not supported");

    if ((memo == "redeem" || memo == "stake") && contract == RWAX_TOKEN_CONTRACT) {
        add_balances(from, {quantity});
    }

    else if (memo == "reward") {
        stakepools_t    stakepools  = get_stakepools(from);
        auto            pool_itr    = stakepools.find(quantity.symbol.code().raw());

        if (pool_itr == stakepools.end()) {
            return;
        }

        stakes_t    stakes          = get_stakes(pool_itr->stake_token.code().raw());
        auto        stake_itr       = stakes.begin();
        asset       total_staked    = asset(0, stake_itr->amount.symbol);
        asset       rest_amount     = quantity;

        while (stake_itr != stakes.end()) {
            total_staked += stake_itr->amount;
            stake_itr++;
        };

        while (stake_itr != stakes.end()) {
            stakes.modify(stake_itr, same_payer, [&](auto& modified_stake) {
                asset cut = asset(quantity.amount * ((double)modified_stake.amount.amount / (double)total_staked.amount), quantity.symbol);
                rest_amount -= cut;
                if (rest_amount.amount < 0) {
                    cut += rest_amount;
                }
                vector<asset> new_tokens = modified_stake.rewarded_tokens;
                bool found = false;
                if (modified_stake.rewarded_tokens.size() == 1 && modified_stake.rewarded_tokens[0] == ZERO_CORE) {
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
    else{
        check(false, "Unsupported memo");
    }
}


void rwax::receive_nft_transfer(
    name from,
    name to,
    vector<uint64_t> asset_ids,
    string memo
) {
    if (to != get_self()) {
        return;
    }

    check(memo.find("deposit") == 0, "Invalid Memo.");

    vector<uint64_t> assets_to_add = asset_ids;

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