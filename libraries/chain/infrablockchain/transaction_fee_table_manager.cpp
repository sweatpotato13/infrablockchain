/**
 *  @file chain/infrablockchain/transaction_fee_table_manager.cpp
 *  @author bezalel@infrablockchain.com
 *  @copyright defined in infrablockchain/LICENSE.txt
 */

#include <infrablockchain/chain/transaction_fee_table_manager.hpp>
#include <infrablockchain/chain/exceptions.hpp>

#include <eosio/chain/database_utils.hpp>

namespace infrablockchain { namespace chain {

   using namespace eosio::chain;

   using transaction_fee_index_set = index_set<
      transaction_fee_table_multi_index
   >;

   transaction_fee_table_manager::transaction_fee_table_manager(chainbase::database& db)
      : _db(db) {
   }

   void transaction_fee_table_manager::add_indices() {
      transaction_fee_index_set::add_indices(_db);
   }

   void transaction_fee_table_manager::initialize_database() {

   }

   void transaction_fee_table_manager::add_to_snapshot( const snapshot_writer_ptr& snapshot ) const {
      transaction_fee_index_set::walk_indices([this, &snapshot]( auto utils ){
         using section_t = typename decltype(utils)::index_t::value_type;
         snapshot->write_section<section_t>([this]( auto& section ){
            decltype(utils)::walk(_db, [this, &section]( const auto &row ) {
               section.add_row(row, _db);
            });
         });
      });
   }

   void transaction_fee_table_manager::read_from_snapshot( const snapshot_reader_ptr& snapshot ) {
      transaction_fee_index_set::walk_indices([this, &snapshot]( auto utils ){
         using section_t = typename decltype(utils)::index_t::value_type;
         snapshot->read_section<section_t>([this]( auto& section ) {
            bool more = !section.empty();
            while(more) {
               decltype(utils)::create(_db, [this, &section, &more]( auto &row ) {
                  more = section.read_row(row, _db);
               });
            }
         });
      });
   }

   void transaction_fee_table_manager::set_tx_fee_for_action(
      const account_name& code, const action_name& action, const tx_fee_value_type value, const tx_fee_type_type fee_type ) {

      //EOS_ASSERT( !code.empty() && !action.empty(), invalid_tx_fee_setup_exception, "set_transaction_fee - code and action must be not 0" );
      EOS_ASSERT( value >= 0, infrablockchain_transaction_fee_exception,  "tx fee value must be >= 0" );
      EOS_ASSERT( fee_type == fixed_tx_fee_per_action_type, infrablockchain_transaction_fee_exception,
                  "currently set_tx_fee_for_action supports only fixed_tx_fee_per_action_type" );

      auto* txfee_obj_ptr = _db.find<transaction_fee_object, by_code_action>(boost::make_tuple(code, action));
      if ( txfee_obj_ptr ) {
         _db.modify( *txfee_obj_ptr, [&](transaction_fee_object& txfee_obj) {
            txfee_obj.value = value;
            txfee_obj.fee_type = fee_type;
         });
      } else {
         _db.create<transaction_fee_object>( [&](transaction_fee_object& txfee_obj) {
            txfee_obj.code = code;
            txfee_obj.action = action;
            txfee_obj.value = value;
            txfee_obj.fee_type = fee_type;
         });
      }
   }

   void transaction_fee_table_manager::set_tx_fee_for_common_action(const action_name& action, const tx_fee_value_type value, const tx_fee_type_type fee_type) {
      set_tx_fee_for_action(code_name_for_built_in_actions, action, value, fee_type);
   }

   void transaction_fee_table_manager::set_default_tx_fee(const tx_fee_value_type value, const tx_fee_type_type fee_type) {
      set_tx_fee_for_action(code_name_for_default_tx_fee, action_name_for_default_tx_fee, value, fee_type);
   }

   void transaction_fee_table_manager::unset_tx_fee_entry_for_action(const account_name& code, const action_name& action ) {
      auto* txfee_obj_ptr = _db.find<transaction_fee_object, by_code_action>(boost::make_tuple(code, action));
      EOS_ASSERT( txfee_obj_ptr, infrablockchain_transaction_fee_exception,  "tx fee db row not found" );
      _db.remove( *txfee_obj_ptr );
   }

   tx_fee_for_action transaction_fee_table_manager::get_tx_fee_for_action(const account_name& code, const action_name& action) const {

      auto* txfee_obj_ptr = _db.find<transaction_fee_object, by_code_action>(boost::make_tuple(code, action));
      if ( txfee_obj_ptr ) {
         auto txfee_obj = *txfee_obj_ptr;
         return tx_fee_for_action { txfee_obj.value, txfee_obj.fee_type };
      } else {
         return get_tx_fee_for_common_action(action);
      }
   }

   tx_fee_for_action transaction_fee_table_manager::get_tx_fee_for_common_action(const action_name& action) const {

      auto* txfee_obj_ptr = _db.find<transaction_fee_object, by_code_action>(boost::make_tuple(account_name(0), action));
      if ( txfee_obj_ptr ) {
         auto txfee_obj = *txfee_obj_ptr;
         return tx_fee_for_action { txfee_obj.value, txfee_obj.fee_type };
      } else {
         return get_default_tx_fee();
      }
   }

   tx_fee_for_action transaction_fee_table_manager::get_default_tx_fee() const {

      auto* txfee_obj_ptr = _db.find<transaction_fee_object, by_code_action>(boost::make_tuple(account_name(0), action_name(0)));
      if ( txfee_obj_ptr ) {
         auto txfee_obj = *txfee_obj_ptr;
         return tx_fee_for_action { txfee_obj.value, txfee_obj.fee_type };
      } else {
         return tx_fee_for_action { 10000, fixed_tx_fee_per_action_type };
      }
   }

} } // namespace infrablockchain::chain
