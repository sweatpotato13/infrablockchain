/**
 *  @copyright defined in LICENSE.txt
 */

#pragma once

#include <eosiolib/time.hpp>
#include <yosemitelib/yx_fee.hpp>
#include <string>

namespace yosemite {

    using namespace eosio;
    using std::string;
    using boost::container::flat_set;

    /* digital contract id used as abi input parameter */
    struct dcid {
        account_name creator;
        uint64_t sequence;

        uint128_t to_uint128() const {
            uint128_t result(creator);
            result <<= 64;
            return result | sequence;
        }

        EOSLIB_SERIALIZE(dcid, (creator)(sequence))
    };

    /**
     * Hereby 'contract' of digital contract means the 'documented' and 'sign-required' contract on real-world business or peer-to-peer field.
     * But the parent class 'contract' means the smart contract of blockchain.
     */
    class digital_contract : public fee_contract {
    public:
        explicit digital_contract(account_name self) : fee_contract(self) {
        }

        void create(account_name creator, const string &dochash, const string &adddochash,
                    const vector<account_name> &signers, const time_point_sec &expiration, uint8_t options);
        void addsigners(dcid dc_id, const vector <account_name> &signers);
        void sign(dcid dc_id, account_name signer, const string &signerinfo);
        void upadddochash(dcid dc_id, const string &adddochash);
        void remove(dcid dc_id);

    protected:
        bool check_fee_operation(const uint64_t &operation_name) override;
        void charge_fee(const account_name &payer, uint64_t operation) override;

    private:
        void check_signers_param(const vector <account_name> &signers, flat_set <account_name> &duplicates);
    };

    /* scope = creator */
    struct dcontract_info {
        uint64_t sequence = 0; /* generated sequence for each creator */
        vector<char> dochash{};
        vector<char> adddochash{};
        time_point_sec expiration{};
        uint8_t options = 0;
        vector<account_name> signers{};
        vector<uint8_t> done_signers{}; // includes the indices to signers vector

        uint64_t primary_key() const { return sequence; }
        bool is_signed() const { return !done_signers.empty(); }
    };

    /* scope = signer */
    struct dcontract_signer_info {
        uint64_t id = 0; // just primary key
        uint128_t dc_id_s{}; // dc_id which is serialized to 128 bit
        vector<char> signerinfo{};

        uint64_t primary_key() const { return id; }
        uint128_t by_dc_id_s() const { return dc_id_s; }
    };

    typedef eosio::multi_index<N(dcontracts), dcontract_info> dcontract_index;
    typedef eosio::multi_index<N(signers), dcontract_signer_info,
            indexed_by<N(dcids), const_mem_fun<dcontract_signer_info, uint128_t, &dcontract_signer_info::by_dc_id_s> >
    > dcontract_signer_index;

}
