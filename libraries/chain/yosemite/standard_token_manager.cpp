/**
 *  @file chain/yosemite/standard_token_manager.cpp
 *  @author bezalel@yosemitex.com
 *  @copyright defined in yosemite/LICENSE.txt
 */

#include <yosemite/chain/standard_token_manager.hpp>
#include <yosemite/chain/yosemite_global_property_database.hpp>
#include <yosemite/chain/standard_token_action_types.hpp>
#include <yosemite/chain/exceptions.hpp>

#include <eosio/chain/config.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/database_utils.hpp>

namespace yosemite { namespace chain {

   using namespace eosio::chain;

   using standard_token_db_index_set = index_set<
      token_info_multi_index,
      token_balance_multi_index
   >;

   standard_token_manager::standard_token_manager( chainbase::database& db )
      : _db(db) {
   }

   void standard_token_manager::add_indices() {
      standard_token_db_index_set::add_indices(_db);
   }

   void standard_token_manager::initialize_database() {

   }

   void standard_token_manager::add_to_snapshot( const snapshot_writer_ptr& snapshot ) const {
      standard_token_db_index_set::walk_indices([this, &snapshot]( auto utils ){
         snapshot->write_section<typename decltype(utils)::index_t::value_type>([this]( auto& section ){
            decltype(utils)::walk(_db, [this, &section]( const auto &row ) {
               section.add_row(row, _db);
            });
         });
      });
   }

   void standard_token_manager::read_from_snapshot( const snapshot_reader_ptr& snapshot ) {
      standard_token_db_index_set::walk_indices([this, &snapshot]( auto utils ){
         snapshot->read_section<typename decltype(utils)::index_t::value_type>([this]( auto& section ) {
            bool more = !section.empty();
            while(more) {
               decltype(utils)::create(_db, [this, &section, &more]( auto &row ) {
                  more = section.read_row(row, _db);
               });
            }
         });
      });
   }

   void standard_token_manager::set_token_meta_info( apply_context& context, const token_id_type &token_id, const token::settokenmeta &token_meta ) {

      int64_t url_size = token_meta.url.size();
      int64_t desc_size = token_meta.description.size();

      EOS_ASSERT( token_meta.symbol.valid(), token_meta_validate_exception, "invalid token symbol" );
      EOS_ASSERT( url_size > 0 && url_size <= 255, token_meta_validate_exception, "invalid token url size" );
      EOS_ASSERT( desc_size > 0 && desc_size <= 255, token_meta_validate_exception, "invalid token description size" );

      auto set_token_meta_lambda = [&token_meta, token_id, url_size, desc_size](token_meta_object& token_meta) {
         token_meta.token_id = token_id;
         token_meta.symbol = token_meta.symbol;
         token_meta.url.resize(url_size);
         memcpy( token_meta.url.data(), token_meta.url.data(), url_size );
         token_meta.description.resize(desc_size);
         memcpy( token_meta.description.data(), token_meta.description.data(), desc_size );
      };

      auto* token_meta_ptr = _db.find<token_meta_object, by_token_id>(token_id);
      if ( token_meta_ptr ) {
         EOS_ASSERT( token_meta_ptr->symbol == token_meta.symbol, token_meta_validate_exception, "token symbol cannot be modified once it is set" );
         EOS_ASSERT( token_meta_ptr->url != token_meta.url.c_str() || token_meta_ptr->description != token_meta.description.c_str(),
                     token_meta_validate_exception, "attempting update token metadata, but new metadata is same as old one" );

         _db.modify( *token_meta_ptr, set_token_meta_lambda );
      } else {
         _db.create<token_meta_object>( set_token_meta_lambda );

         context.add_ram_usage(token_id, (int64_t)(config::billable_size_v<token_meta_object>));
      }
   }

   const token_meta_object* standard_token_manager::get_token_meta_info( const token_id_type &token_id ) {
      return _db.find<token_meta_object, by_token_id>(token_id);
   }

   void standard_token_manager::update_token_total_supply( const token_meta_object* token_meta_ptr, share_type delta ) {
      _db.modify<token_meta_object>( *token_meta_ptr, [&](token_meta_object& token_meta) {
         token_meta.total_supply += delta;
      });
   }

   void standard_token_manager::add_token_balance( apply_context& context, token_id_type token_id, account_name owner, share_type value ) {

      EOS_ASSERT( context.receiver == token_id, invalid_token_balance_update_access_exception, "add_token_balance : action context receiver mismatches token-id" );

      auto* balance_ptr = _db.find<token_balance_object, by_token_account>(boost::make_tuple(token_id, owner));
      if ( balance_ptr ) {
         _db.modify<token_balance_object>( *balance_ptr, [&]( token_balance_object& balance_obj ) {
            balance_obj.balance += value;
         });
      } else {
         _db.create<token_balance_object>( [&](token_balance_object& balance_obj) {
            balance_obj.token_id = token_id;
            balance_obj.account = owner;
            balance_obj.balance = value;
         });

         context.add_ram_usage(owner, (int64_t)(config::billable_size_v<token_balance_object>));
      }
   }

   void standard_token_manager::subtract_token_balance( apply_context& context, token_id_type token_id, account_name owner, share_type value ) {

      EOS_ASSERT( context.receiver == token_id, invalid_token_balance_update_access_exception, "subtract_token_balance : action context receiver mismatches token-id" );

      auto* balance_ptr = _db.find<token_balance_object, by_token_account>(boost::make_tuple(token_id, owner));
      if ( balance_ptr ) {
         share_type cur_balance = balance_ptr->balance;
         EOS_ASSERT( cur_balance >= value, insufficient_token_balance_exception, "account ${account} has insufficient_token_balance", ("account", owner) );

         if ( cur_balance == value ) {
            _db.remove( *balance_ptr );
            context.add_ram_usage(owner, -(int64_t)(config::billable_size_v<token_balance_object>));
         } else {
            _db.modify<token_balance_object>( *balance_ptr, [&]( token_balance_object& balance_obj ) {
               balance_obj.balance -= value;
            });
         }

      } else {
         EOS_ASSERT( false, insufficient_token_balance_exception, "account ${account} has insufficient_token_balance", ("account", owner) );
      }
   }

   void standard_token_manager::pay_transaction_fee( transaction_context& trx_context, account_name fee_payer, int64_t fee_amount ) {

      EOS_ASSERT( fee_amount > 0, yosemite_transaction_fee_exception, "transaction fee amount must be greater than 0" );

      auto& sys_tokens = _db.get<yosemite_global_property_object>().system_token_list;

      for( const auto& sys_token : sys_tokens.system_tokens ) {

         auto sys_token_id = sys_token.token_id;

         auto* balance_ptr = _db.find<token_balance_object, by_token_account>(boost::make_tuple(sys_token_id, fee_payer));
         if ( balance_ptr ) {

            int64_t fee_amount_for_this_token = fee_amount;
            share_type cur_balance = balance_ptr->balance;

            if (sys_token.token_weight != system_token::token_weight_1x) {
               fee_amount_for_this_token = fee_amount_for_this_token * system_token::token_weight_1x / sys_token.token_weight;
               if ( cur_balance >= fee_amount_for_this_token ) {
                  fee_amount = 0;
               } else {
                  fee_amount_for_this_token = cur_balance;
                  fee_amount -= cur_balance * sys_token.token_weight / system_token::token_weight_1x;
               }
            } else {
               if ( cur_balance >= fee_amount_for_this_token ) {
                  fee_amount = 0;
               } else {
                  fee_amount_for_this_token = cur_balance;
                  fee_amount -= cur_balance;
               }
            }

            auto token_meta_ptr = _db.find<token_meta_object, by_token_id>(sys_token_id);
            EOS_ASSERT( token_meta_ptr != nullptr, yosemite_transaction_fee_exception, "no token meta info for system token ${token_id}", ("token_id", sys_token_id) );

            // dispatch 'txfee' action for this system token
            trx_context.trace->action_traces.emplace_back();
            trx_context.dispatch_action( trx_context.trace->action_traces.back(),
               action { vector<permission_level>{ {fee_payer, config::active_name} },
                        sys_token_id,
                        token::txfee{ fee_payer, asset(fee_amount_for_this_token, token_meta_ptr->symbol) }
               } );

            if (fee_amount <= 0) break;
         }
      }

      EOS_ASSERT( fee_amount <= 0, yosemite_transaction_fee_exception, "fee payer ${payer} does not have enough system token", ("payer", fee_payer) );
   }

} } // namespace yosemite::chain
