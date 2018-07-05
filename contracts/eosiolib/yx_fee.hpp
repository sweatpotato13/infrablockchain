#ifndef YX_FEE_HPP
#define YX_FEE_HPP

#include <eosiolib/eosio.hpp>
#include <eosiolib/symbol.hpp>

namespace yosemite {
    static const uint64_t NATIVE_TOKEN_NAME = S(0, DKRW) >> 8;
    static const uint64_t NATIVE_TOKEN = S(4, DKRW);
    static const account_name FEEDIST_ACCOUNT_NAME = N(yx.feedist);

    class fee_contract : public eosio::contract {
    public:
        explicit fee_contract(account_name self) : eosio::contract(self) {
        }

        virtual ~fee_contract() {}

        void setfee(const eosio::name &operation_name, const eosio::asset &fee) {
            //require_auth(N(eosio.prods)); //TODO:multisig

            eosio_assert(static_cast<uint32_t>(fee.is_valid()), "wrong fee format");
            eosio_assert(static_cast<uint32_t>(fee.amount >= 0), "negative fee is not allowed");
            eosio_assert(static_cast<uint32_t>(fee.symbol.value == NATIVE_TOKEN), "only the native token is allowed for fee");
            eosio_assert(static_cast<uint32_t>(check_fee_operation(operation_name.value)), "wrong operation name");

            fees fee_table(get_self(), get_self());

            const auto &holder = fee_table.find(operation_name.value);
            if (holder == fee_table.end()) {
                fee_table.emplace(get_self(), [&](auto& a) {
                    a.operation = operation_name.value;
                    a.fee = fee;
                });
            } else {
                fee_table.modify(holder, 0, [&](auto& a) {
                    a.fee = fee;
                });
            }
        }

    protected:
        virtual bool check_fee_operation(const uint64_t &operation_name) = 0;
        virtual void charge_fee(const account_name &payer, uint64_t operation) = 0;

        struct fee_holder {
            uint64_t operation;
            eosio::asset fee;

            uint64_t primary_key() const { return operation; }
        };

        typedef eosio::multi_index<N(fees), fee_holder> fees;
    };
}

#endif //YX_FEE_HPP
