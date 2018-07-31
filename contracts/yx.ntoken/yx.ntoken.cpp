#include "yx.ntoken.hpp"
#include <yosemitelib/identity.hpp>
#include <yosemitelib/system_accounts.hpp>
#include <yosemitelib/transaction_fee.hpp>
#include <yosemitelib/system_depository.hpp>


namespace yosemite {

    bool ntoken::check_identity_auth_for_transfer(account_name account, const ntoken_kyc_rule_type &kycrule_type) {
        eosio_assert(static_cast<uint32_t>(!has_account_state(account, YOSEMITE_ID_ACC_STATE_BLACKLISTED)),
                     "account is blacklisted by identity authority");
        switch (kycrule_type) {
            case NTOKEN_KYC_RULE_TYPE_TRANSFER_SEND:
                eosio_assert(static_cast<uint32_t>(!has_account_state(account, YOSEMITE_ID_ACC_STATE_BLACKLISTED_NTOKEN_SEND)),
                             "account is send-blacklisted by identity authority");
                break;
            case NTOKEN_KYC_RULE_TYPE_TRANSFER_RECEIVE:
                eosio_assert(static_cast<uint32_t>(!has_account_state(account, YOSEMITE_ID_ACC_STATE_BLACKLISTED_NTOKEN_RECEIVE)),
                             "account is receive-blacklisted by identity authority");
                break;
            case NTOKEN_KYC_RULE_TYPE_MAX:
                // ignored
                break;
        }

        kyc_rule_index kyc_rule(get_self(), get_self());
        const auto &rule = kyc_rule.get(kycrule_type, "KYC rule is not set; use setkycrule operation to set");

        return has_all_kyc_status(account, rule.kyc_flags);
    }

    void ntoken::nissue(const account_name &to, const yx_asset &token, const string &memo) {
        eosio_assert(static_cast<uint32_t>(token.is_valid()), "invalid token");
        eosio_assert(static_cast<uint32_t>(token.amount > 0), "must be positive token");
        eosio_assert(static_cast<uint32_t>(token.symbol.value == YOSEMITE_NATIVE_TOKEN_SYMBOL),
                     "cannot issue non-native token with this operation or wrong precision is specified");
        eosio_assert(static_cast<uint32_t>(memo.size() <= 256), "memo has more than 256 bytes");

        require_auth(token.issuer);
        eosio_assert(static_cast<uint32_t>(is_authorized_sys_depository(token.issuer)),
                     "issuer account is not system depository");

        stats_native stats(get_self(), token.issuer);
        const auto &tstats = stats.find(NTOKEN_BASIC_STATS_KEY);

        if (tstats == stats.end()) {
            stats.emplace(get_self(), [&](auto &s) {
                s.key = NTOKEN_BASIC_STATS_KEY;
                s.supply = token.amount;
            });
        } else {
            charge_fee(token.issuer, YOSEMITE_TX_FEE_OP_NAME_NTOKEN_ISSUE);

            stats.modify(tstats, 0, [&](auto &s) {
                s.supply += token.amount;
                eosio_assert(static_cast<uint32_t>(s.supply > 0 && s.supply <= asset::max_amount),
                             "cannot issue token more than 2^62 - 1");
            });
        }

        add_native_token_balance(token.issuer, token);

        if (to != token.issuer) {
            INLINE_ACTION_SENDER(yosemite::ntoken, ntransfer)
                    (get_self(), {{token.issuer, N(active)}, {YOSEMITE_SYSTEM_ACCOUNT, N(active)}},
                     { token.issuer, to, token, memo });
        }
    }

    void ntoken::nredeem(const yx_asset &token, const string &memo) {
        eosio_assert(static_cast<uint32_t>(token.is_valid()), "invalid token");
        eosio_assert(static_cast<uint32_t>(token.amount > 0), "must be positive token");
        eosio_assert(static_cast<uint32_t>(token.symbol.value == YOSEMITE_NATIVE_TOKEN_SYMBOL), "cannot redeem non-native token with this operation or wrong precision is specified");
        eosio_assert(static_cast<uint32_t>(memo.size() <= 256), "memo has more than 256 bytes");

        require_auth(token.issuer);
        eosio_assert(static_cast<uint32_t>(is_authorized_sys_depository(token.issuer)),
                     "issuer account is not system depository");

        stats_native stats(get_self(), token.issuer);
        const auto &tstats = stats.get(NTOKEN_BASIC_STATS_KEY, "createn for the issuer is not called");
        eosio_assert(static_cast<uint32_t>(tstats.supply >= token.amount), "insufficient supply of the native token of the specified depository");

        charge_fee(token.issuer, YOSEMITE_TX_FEE_OP_NAME_NTOKEN_REDEEM);

        stats.modify(tstats, 0, [&](auto &s) {
            s.supply -= token.amount;
        });

        sub_native_token_balance(token.issuer, token);
    }

    void ntoken::transfer(account_name from, account_name to, eosio::asset token, const string &memo) {
        wptransfer(from, to, token, from, memo);
    }

    void ntoken::wptransfer(account_name from, account_name to, eosio::asset token, account_name payer, const string &memo) {
        eosio::asset txfee_amount;

        if (has_auth(YOSEMITE_SYSTEM_ACCOUNT)) {
            txfee_amount = eosio::asset{0, YOSEMITE_NATIVE_TOKEN_SYMBOL};
        } else {
            eosio_assert(static_cast<uint32_t>(token.is_valid()), "invalid token");
            eosio_assert(static_cast<uint32_t>(token.amount > 0), "must transfer positive token");
            eosio_assert(static_cast<uint32_t>(token.symbol.value == YOSEMITE_NATIVE_TOKEN_SYMBOL),
                         "only native token is supported; use yx.token::transfer instead");
            eosio_assert(static_cast<uint32_t>(from != to), "from and to account cannot be the same");
            eosio_assert(static_cast<uint32_t>(memo.size() <= 256), "memo has more than 256 bytes");

            require_auth(from);
            eosio_assert(static_cast<uint32_t>(is_account(to)), "to account does not exist");

            txfee_amount = charge_fee(payer, YOSEMITE_TX_FEE_OP_NAME_NTOKEN_TRANSFER);
        }

        // NOTE:We don't need notification to from and to account here. It's done by several ntrasfer operation.

        eosio_assert(static_cast<uint32_t>(check_identity_auth_for_transfer(from, NTOKEN_KYC_RULE_TYPE_TRANSFER_SEND)),
                     "KYC authentication for from account is failed");
        eosio_assert(static_cast<uint32_t>(check_identity_auth_for_transfer(to, NTOKEN_KYC_RULE_TYPE_TRANSFER_RECEIVE)),
                     "KYC authentication for to account is failed");

        accounts_native_total from_total(get_self(), from);
        const auto &total_holder = from_total.get(NTOKEN_TOTAL_BALANCE_KEY, "from account doesn't have native token balance");
        if (txfee_amount.amount > 0) {
            int64_t sum = token.amount + txfee_amount.amount;
            eosio_assert(static_cast<uint32_t>(sum > 0 && sum <= asset::max_amount), "sum with token and fee amount cannot be more than 2^62 - 1");
            eosio_assert(static_cast<uint32_t>(total_holder.amount >= sum), "insufficient native token balance");
        }

        accounts_native accounts_table_native(get_self(), from);
        for (auto &from_balance_holder : accounts_table_native) {
            if (token.amount == 0) {
                break;
            }

            // consider transaction fee by calculation because fee transaction is inline transaction
            // (not reflected at this time)
            int64_t from_balance = 0;
            if (txfee_amount.amount > 0) {
                if (from_balance_holder.amount <= txfee_amount.amount) {
                    from_balance = 0;
                    txfee_amount.amount -= from_balance_holder.amount;
                } else {
                    from_balance = from_balance_holder.amount - txfee_amount.amount;
                    txfee_amount.amount = 0;
                }
            } else {
                from_balance = from_balance_holder.amount;
            }
            if (from_balance <= 0) {
                continue;
            }

            int64_t to_balance = 0;
            if (from_balance <= token.amount) {
                to_balance = from_balance;
                token.amount -= to_balance;
            } else {
                to_balance = token.amount;
                token.amount = 0;
            }

            yx_symbol native_token_symbol{YOSEMITE_NATIVE_TOKEN_SYMBOL, from_balance_holder.depository};

            if (from == payer) {
                INLINE_ACTION_SENDER(yosemite::ntoken, ntransfer)
                        (get_self(), {{from, N(active)}, {YOSEMITE_SYSTEM_ACCOUNT, N(active)}},
                         { from, to, {to_balance, native_token_symbol}, memo });
            } else {
                INLINE_ACTION_SENDER(yosemite::ntoken, wpntransfer)
                        (get_self(), {{from, N(active)}, {YOSEMITE_SYSTEM_ACCOUNT, N(active)}},
                         { from, to, {to_balance, native_token_symbol}, payer, memo });
            }
        }
    }

    void ntoken::ntransfer(account_name from, account_name to, const yx_asset &token, const string &memo) {
        wpntransfer(from, to, token, from, memo);
    }

    void ntoken::wpntransfer(account_name from, account_name to, const yx_asset &token, account_name payer,
                           const string &memo) {
        if (!has_auth(YOSEMITE_SYSTEM_ACCOUNT)) {
            eosio_assert(static_cast<uint32_t>(token.is_valid()), "invalid token");
            eosio_assert(static_cast<uint32_t>(token.amount > 0), "must transfer positive token");
            eosio_assert(static_cast<uint32_t>(token.symbol.value == YOSEMITE_NATIVE_TOKEN_SYMBOL),
                         "cannot transfer non-native token with this operation or wrong precision is specified");
            eosio_assert(static_cast<uint32_t>(from != to), "from and to account cannot be the same");
            eosio_assert(static_cast<uint32_t>(memo.size() <= 256), "memo has more than 256 bytes");

            require_auth(from);
            eosio_assert(static_cast<uint32_t>(is_account(to)), "to account does not exist");

            charge_fee(payer, YOSEMITE_TX_FEE_OP_NAME_NTOKEN_NTRANSFER);
        }

        eosio_assert(static_cast<uint32_t>(check_identity_auth_for_transfer(from, NTOKEN_KYC_RULE_TYPE_TRANSFER_SEND)),
                     "KYC authentication for from account is failed");
        eosio_assert(static_cast<uint32_t>(check_identity_auth_for_transfer(to, NTOKEN_KYC_RULE_TYPE_TRANSFER_RECEIVE)),
                     "KYC authentication for to account is failed");

        require_recipient(from);
        require_recipient(to);

        sub_native_token_balance(from, token);
        add_native_token_balance(to, token);
    }

    void ntoken::payfee(account_name payer, asset token) {
        require_auth(payer);
        require_auth(YOSEMITE_SYSTEM_ACCOUNT);

        if (token.amount == 0) return;

        accounts_native_total from_total(get_self(), payer);
        const auto &total_holder = from_total.get(NTOKEN_TOTAL_BALANCE_KEY, "payer doesn't have native token balance");
        eosio_assert(static_cast<uint32_t>(total_holder.amount >= token.amount), "insufficient native token balance");

        eosio_assert(static_cast<uint32_t>(check_identity_auth_for_transfer(payer, NTOKEN_KYC_RULE_TYPE_TRANSFER_SEND)),
                     "KYC authentication for fee payer account is failed");

        accounts_native accounts_table_native(get_self(), payer);
        for (auto &from_balance_holder : accounts_table_native) {
            if (token.amount == 0) {
                break;
            }

            int64_t to_balance = 0;

            if (from_balance_holder.amount <= token.amount) {
                to_balance = from_balance_holder.amount;
                token.amount -= to_balance;
            } else {
                to_balance = token.amount;
                token.amount = 0;
            }

            yx_symbol native_token_symbol{YOSEMITE_NATIVE_TOKEN_SYMBOL, from_balance_holder.depository};
            INLINE_ACTION_SENDER(yosemite::ntoken, feetransfer)
                    (get_self(), {{payer, N(active)},{get_self(), N(active)}},
                     { payer, {to_balance, native_token_symbol} });
        }
    }

    // Assume that payfee operation is called only.
    void ntoken::feetransfer(account_name payer, const yx_asset &token) {
        require_auth(payer);
        require_auth(get_self());

        require_recipient(payer);
        require_recipient(YOSEMITE_TX_FEE_ACCOUNT);

        sub_native_token_balance(payer, token);
        add_native_token_balance(YOSEMITE_TX_FEE_ACCOUNT, token);
    }

    asset ntoken::charge_fee(const account_name &payer, uint64_t operation) {
        auto tx_fee = yosemite::get_transaction_fee(operation);

        if (tx_fee.amount > 0) {
            INLINE_ACTION_SENDER(yosemite::ntoken, payfee)
                    (get_self(), {{payer, N(active)}, {YOSEMITE_SYSTEM_ACCOUNT, N(active)}},
                     {payer, tx_fee});
        }

        return tx_fee;
    }

    void ntoken::add_native_token_balance(const account_name &owner, const yx_asset &token) {
        accounts_native accounts_table_native(get_self(), owner);
        const auto &native_holder = accounts_table_native.find(token.issuer);

        if (native_holder == accounts_table_native.end()) {
            accounts_table_native.emplace(get_self(), [&](auto &holder) {
                holder.depository = token.issuer;
                holder.amount = token.amount;
            });
        } else {
            accounts_table_native.modify(native_holder, 0, [&](auto &holder) {
                holder.amount += token.amount;
                eosio_assert(static_cast<uint32_t>(holder.amount > 0 && holder.amount <= asset::max_amount), "token amount cannot be more than 2^62 - 1");
            });
        }

        accounts_native_total accounts_table_total(get_self(), owner);
        const auto &total_holder = accounts_table_total.find(NTOKEN_TOTAL_BALANCE_KEY);
        if (total_holder == accounts_table_total.end()) {
            accounts_table_total.emplace(get_self(), [&](auto &tot_holder) {
                tot_holder.amount = token.amount;
            });
        } else {
            accounts_table_total.modify(total_holder, 0, [&](auto &tot_holder) {
                tot_holder.amount += token.amount;
                eosio_assert(static_cast<uint32_t>(tot_holder.amount > 0 && tot_holder.amount <= asset::max_amount), "token amount cannot be more than 2^62 - 1");
            });
        }
    }

    void ntoken::sub_native_token_balance(const account_name &owner, const yx_asset &token) {
        accounts_native accounts_table_native(get_self(), owner);
        const auto &native_holder = accounts_table_native.get(token.issuer, "account doesn't have native token of the specified depository");
        eosio_assert(static_cast<uint32_t>(native_holder.amount >= token.amount), "insufficient native token of the specified depository");

        accounts_table_native.modify(native_holder, 0, [&](auto &holder) {
            holder.amount -= token.amount;
        });

        accounts_native_total accounts_table_total(get_self(), owner);
        const auto &total_holder = accounts_table_total.get(NTOKEN_TOTAL_BALANCE_KEY, "account doesn't have native token balance");
        eosio_assert(static_cast<uint32_t>(total_holder.amount >= token.amount), "insufficient total native token");

        accounts_table_total.modify(total_holder, 0, [&](auto &tot_holder) {
            tot_holder.amount -= token.amount;
        });
    }

    void ntoken::setkycrule(uint8_t type, uint16_t kyc) {
        eosio_assert(static_cast<uint32_t>(type < NTOKEN_KYC_RULE_TYPE_MAX), "invalid type");
//        eosio_assert(static_cast<uint32_t>(is_valid_kyc_status(kyc)), "invalid kyc flags");
        require_auth(YOSEMITE_SYSTEM_ACCOUNT);

        kyc_rule_index kyc_rule(get_self(), get_self());
        auto itr = kyc_rule.find(type);

        if (itr == kyc_rule.end()) {
            kyc_rule.emplace(get_self(), [&](auto &holder) {
                holder.type = type;
                holder.kyc_flags = kyc;
            });
        } else {
            kyc_rule.modify(itr, 0, [&](auto &holder) {
                holder.kyc_flags = kyc;
            });
        }
    }

}

EOSIO_ABI(yosemite::ntoken, (nissue)(nredeem)(transfer)(wptransfer)(ntransfer)(wpntransfer)
                            (payfee)(feetransfer)(setkycrule)
)
