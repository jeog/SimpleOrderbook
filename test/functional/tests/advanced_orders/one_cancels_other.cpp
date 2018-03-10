/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include "../../functional.hpp"

#ifdef RUN_FUNCTIONAL_TESTS

#include <set>
#include <map>
#include <vector>
#include <tuple>
#include <iostream>
#include <iomanip>
#include <stdexcept>

using namespace std;
using namespace sob;

namespace {
    map<id_type, id_type> ids;
    auto ecb = create_advanced_callback(ids);
    size_t sz = 100;

    inline void
    id_insert(id_type id)
    { ids[id] = id; }
}

int
TEST_advanced_OCO_1(sob::FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    size_t nticks = orderbook->ticks_in_range();

    ids.clear();

    int n = static_cast<int>(nticks/3);
    if( n <= 4 ){
        return 1;
    }

    for( int i = 0; i < n; ++ i ){
        auto aot = AdvancedOrderTicketOCO::build_limit( false, conv(end-i*incr),
                                                        sz);
        id_type id = orderbook->insert_limit_order( true, conv(beg+i*incr),
                                                    sz, ecb, aot );
        ids[id] = id;
        order_info of = orderbook->get_order_info(id);
        if( of.limit != conv(beg+i*incr)
            || !of.is_buy
            || of.size != sz
            || of.type != order_type::limit
            || of.advanced.condition() != order_condition::one_cancels_other)
        {
            return 2;
        }
        const OrderParamaters* op = of.advanced.order1();
        if( !op->is_by_price()
            || op->limit_price() !=  orderbook->price_to_tick(end - (i*incr))
            || op->is_buy()
            || op->size() != sz )
        {
            return 3;
        }
        cout<< "ORDER INFO: " << id << " "<< of << endl;
    }
    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();

    /*
     *                  ...
     *                  - (n - 4) (fill half)
     *                  - (n - 3) (cancel)
     *                  - (n - 2) (fill half) (fill half)
     *                  - (n - 1) (cancel)
     *
     *  + n + 1 (fill)
     *  + n + 2 (cancel)
     *  + n + 3 (fill)
     *  + n + 4 (cancel)
     *  ...
     */
    id_insert( orderbook->insert_market_order(false, sz) );
    id_insert( orderbook->insert_limit_order(true, end, sz/2) );
    id_insert( orderbook->insert_limit_order(false, beg, sz) );
    id_insert( orderbook->insert_market_order(true, sz) );

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();

    if( bp != conv(beg + (n-5) * incr) ){
        return 4;
    }else if( ap !=  conv(end - (n-5+1) *incr ) ){
        return 5;
    }else if( bs != sz ){
        return 6;
    }else if( as != static_cast<size_t>(sz/2) ){
        return 7;
    }else if( vol != static_cast<unsigned long>(sz*3.5) ){
        return 8;
    }

    auto md_book = orderbook->market_depth(nticks + 2);
    size_t book_sz = md_book.size();
    if(book_sz != static_cast<size_t>(n-4) * 2 + 1){
        return 9;
    }

    cout<< "pulls: " << boolalpha <<endl;
    for( auto id : ids ){
        if( orderbook->pull_order(id.second) ){
            cout<< id.second << " ";
        }
    }
    cout<< endl;
    ids.clear();

    md_book = orderbook->market_depth(nticks + 2);
    cout<< "market depth: " << endl;
    for(auto& p : md_book){
        cout<< setw(10) << p.first << " " << p.second.first << " "
            << p.second.second << endl;
    }

    book_sz = md_book.size();
    if( book_sz > 0 ){
        return 10;
    }else if( orderbook->total_bid_size() || orderbook->total_ask_size() ){
        return 11;
    }

    return 0;
}


int
TEST_advanced_OCO_2(sob::FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    size_t nticks = orderbook->ticks_in_range();

    ids.clear();

    id_insert( orderbook->insert_limit_order(true, beg, sz) );
    id_insert( orderbook->insert_limit_order(true, conv(beg+2*incr), sz) );
    id_insert( orderbook->insert_limit_order(true, conv(beg+4*incr), sz) );
    id_insert( orderbook->insert_limit_order(false, conv(end-4*incr), sz) );
    id_insert( orderbook->insert_limit_order(false, conv(end-2*incr), sz) );
    id_insert( orderbook->insert_limit_order(false, end, sz) );

    auto aot = AdvancedOrderTicketOCO::build_stop(true, conv(end-4*incr), sz);
    id_type id = orderbook->insert_stop_order( false, conv(beg+4*incr), sz,
                                               ecb, aot );
    id_insert(id);

    order_info of = orderbook->get_order_info(id);
    if( of.limit != 0
        || of.stop != conv(beg+4*incr)
        || of.size != sz
        || of.type != order_type::stop
        || of.advanced.condition() != order_condition::one_cancels_other )
    {
        return 1;
    }

    const OrderParamaters *op = of.advanced.order1();
    if( !op->is_by_price()
        || op->stop_price() != conv(end-4*incr)
        || op->size() != sz )
    {
        return 2;
    }
    cout<< "ORDER INFO: " << id << " "<< of << endl;

    auto aot2 = AdvancedOrderTicketOCO::build_stop_limit( false, conv(beg+incr),
                                                          conv(beg+incr), sz );
    id = orderbook->insert_stop_order( true, conv(end-incr), conv(end-incr),
                                       sz, ecb, aot2 );
    id_insert(id);

    of = orderbook->get_order_info(id);
    if( of.limit != conv(end-incr)
        || of.stop != conv(end-incr)
        || of.size != sz
        || of.type != order_type::stop_limit
        || of.advanced.condition() != order_condition::one_cancels_other )
    {
        return 3;
    }

    op = of.advanced.order1();
    if( !op->is_by_price()
        || op->limit_price() !=  conv(beg+incr)
        || op->stop_price() != conv(beg+incr)
        || op->size() != sz  )
    {
        return 4;
    }
    cout<< "ORDER INFO: " << id << " "<< of << endl;

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();
    /*
     *                                 end
     *                                 end - 1 (3: cancel stop #2.b) (5: limit buy)
     *                                 end - 2 (4: fill .5) (5: fill .5)
     *                                 end - 3
     *                                 end - 4 (1: cancel stop #1.a) (2: fill .5) (4: fill .5)
     *
     *
     *  beg + 4 (1: fill) -------------|
     *  beg + 3                        |
     *  beg + 2 (1: fill stop #1.b) <--|
     *  beg + 1 (3: limit sell, stop #2.a) <--| (4: fill)
     *  beg     (3: fill) --------------------|
     *
     *  300 200 100 0 100               300 250 350 250 200 100
     */
    id_insert( orderbook->insert_market_order(false, sz) );     // 1
    id_insert( orderbook->insert_limit_order(true, end, sz/2) ); // 2
    id_insert( orderbook->insert_limit_order(false, beg, sz) ); // 3
    id_insert( orderbook->insert_market_order(true, sz *2) );      // 4
    id_insert( orderbook->insert_limit_order( true, conv(end-incr),
                                              static_cast<size_t>(1.5*sz) ) ); //5

    cout<< "time and sales: " << endl;
    for( auto& t : orderbook->time_and_sales() ){
        cout<< setw(10) << to_string(get<0>(t)) << " " << get<1>(t) << " "
            << get<2>(t) << endl;
    }
    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();

    double bp = orderbook->bid_price();
    size_t tbp = orderbook->total_bid_size();
    size_t tap = orderbook->total_ask_size();
    double ap = orderbook->ask_price();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();

    if( bp != conv(end-incr) ){
        return 5;
    }else if( ap !=  end ){
        return 6;
    }else if( bs != sz ){
        return 7;
    }else if( as != sz ){
        return 8;
    }else if( tbp != sz){
        return 9;
    }else if( tap != sz ){
        return 10;
    }else if( vol != 6*sz ){
        return 11;
    }

    auto md_book = orderbook->market_depth(nticks + 2);
    size_t book_sz = md_book.size();
    if( book_sz != 2 ){
        return 12;
    }

    cout<< "pulls: " << boolalpha <<endl;
    for(auto id : ids ){
        if( orderbook->pull_order(id.second) ){
            cout<< id.second << " ";
        }
    }
    cout<< endl;
    ids.clear();

    md_book = orderbook->market_depth(nticks + 2);
    cout<< "market depth: " << endl;
    for(auto& p : md_book){
        cout<< setw(10) << p.first << " " << p.second.first << " "
            << p.second.second << endl;
    }

    book_sz = md_book.size();
    if( book_sz > 0 ){
        return 13;
    }else if( orderbook->total_bid_size() || orderbook->total_ask_size() ){
        return 14;
    }

    return 0;
}


int
TEST_advanced_OCO_3(sob::FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    // check for market/* OCO
    try{
        auto aot = AdvancedOrderTicketOCO::build_stop(false, conv(mid-incr), sz);
        orderbook->insert_market_order(true, sz, ecb, aot);
        return 1;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for limit/market OCO
    try{
        auto aot = AdvancedOrderTicketOTO::build_market(false, sz);
        aot.change_condition( order_condition::one_cancels_other );
        orderbook->insert_limit_order(true, mid, sz, ecb, aot);
        return 2;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for limit/limit OCO where buy price >= sell price
    try{
        auto aot = AdvancedOrderTicketOCO::build_limit(false, conv(mid-incr), sz);
        orderbook->insert_limit_order(true, mid, sz, ecb, aot);
        return 3;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for limit/limit OCO where sell price <= buy price
    try{
        auto aot = AdvancedOrderTicketOCO::build_limit(true, conv(beg+incr), sz);
        orderbook->insert_limit_order(false, beg, sz, ecb, aot);
        return 4;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for limit/limit OCO where buy limit == buy limit
    try{
        auto aot = AdvancedOrderTicketOCO::build_limit(true, end, sz);
        orderbook->insert_limit_order(true, end, sz, ecb, aot);
        return 5;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for malformed order ticket
    try{
        auto aot = AdvancedOrderTicketOTO::build_stop_limit(true, beg, end, sz);
        aot.change_order1( OrderParamatersByPrice(true, 0, end, beg) );
        orderbook->insert_limit_order(true, end, sz, ecb, aot);
        return 6;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for bad limit ticket prices
    try{
        auto aot = AdvancedOrderTicketOTO::build_limit(true, conv(end+incr), sz);
        orderbook->insert_stop_order(true, end, sz, ecb, aot);
        return 7;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for bad stop ticket prices
    try{
        auto aot = AdvancedOrderTicketOTO::build_stop(true, conv(end+incr), sz);
        orderbook->insert_stop_order(true, end, end, sz, ecb, aot);
        return 8;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for bad stop limit ticket prices
    try{
        auto aot = AdvancedOrderTicketOCO::build_stop_limit( false, beg,
                                                             conv(beg-incr), sz );
        orderbook->insert_limit_order(true, end, sz, ecb, aot);
        return 9;
    }catch(advanced_order_error& e){
        cout<< "sucessfully caught aot error: " << e.what() << endl;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid arg: " << e.what() << endl;
    }

    // check for bad ticket size
    try{
        AdvancedOrderTicketOTO::build_market(false, 0);
        return 10;
    }catch(invalid_argument& e){
        cout<< "sucessfully caught invalid argument: " << e.what() << endl;
    }

    return 0;
}

int
TEST_advanced_OCO_4(sob::FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();
    size_t nticks = orderbook->ticks_in_range();

    ids.clear();

    id_insert( orderbook->insert_limit_order(false, mid, sz, ecb)); //1

    auto aot = AdvancedOrderTicketOCO::build_limit(
            true, conv(mid+incr), sz, condition_trigger::fill_partial
            );
    id_insert( orderbook->insert_limit_order(true, mid, sz*2, ecb, aot) ); //2

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    auto aot2 = AdvancedOrderTicketOTO::build_limit(
            false, conv(mid+incr), sz, condition_trigger::fill_full
            );
    id_type id1 = orderbook->insert_limit_order(false, mid, sz*3, ecb, aot2); //3
    id_insert(id1);

    order_info of = orderbook->get_order_info(id1);
    if( of.type != order_type::limit
        || of.advanced.trigger() != condition_trigger::fill_full
        || of.advanced.condition() != order_condition::one_triggers_other )
    {
        return 1;
    }

    const OrderParamaters *op = of.advanced.order1();
    if( !op->is_by_price()
        || !op->is_limit_order()
        || op->size() != sz )
    {
        return 2;
    }
    cout<< "ORDER INFO: " << id1 << " "<< of << endl;

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    id_insert( orderbook->insert_limit_order(true, conv(mid+incr), sz*3, ecb) ); //4
    /*
     *                       (4.c, B100)                             (4.b, S100)
     *                            |                                        |
     *                            |                                        |                                       |
     * mid (2, B100)(2, B100)(4.a, B200)          (1, S100) (3, S100) (3, S200)
     *                            |_______________________________________|
     *
     *
     *  S100    0    0  S200  0   S100   0
     *        B100 B100  0    0   B100   0
     *
     *   0    100  100  200  400  400   500
     */
    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();
    size_t nts = orderbook->time_and_sales().size();

    if( bs != 0 ){
        return 3;
    }else if( as != 0 ){
        return 4;
    }else if( tbs != 0 ){
        return 5;
    }else if( tas != 0 ){
        return 6;
    }else if( vol != 500 ){
        return 7;
    }else if( nts != 4 ){
        return 8;
    }

    auto md_book = orderbook->market_depth(nticks + 2);
    size_t book_sz = md_book.size();
    if( book_sz > 0 ){
        return 9;
    }

    id_insert( orderbook->insert_limit_order(false, mid, sz, ecb)); //1
    aot = AdvancedOrderTicketOCO::build_limit(
            true, mid, sz, condition_trigger::fill_full
            );
    id_insert( orderbook->insert_limit_order( true, conv(mid+incr),
                                              sz, ecb, aot) ); //2

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    if( orderbook->market_depth(nticks+2).size() > 0 ){
        return 10;
    }

    if( orderbook->volume() != vol+100 ){
        return 11;
    }

    id_insert( orderbook->insert_limit_order(false, beg, sz, ecb) ); //1
    id_insert( orderbook->insert_limit_order(false, conv(beg+incr), sz, ecb) ); //1
    aot = AdvancedOrderTicketOCO::build_limit(
            true, beg, sz, condition_trigger::fill_full
            );
    id_insert( orderbook->insert_limit_order( true, conv(beg+incr), sz*2,
                                              ecb, aot) ); //2

    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    if( orderbook->market_depth(nticks+2).size() > 0 ){
        return 12;
    }

    if( orderbook->volume() != vol+100+200 ){
        return 13;
    }

    aot2 = AdvancedOrderTicketOTO::build_stop(
            false, conv(beg+incr), sz, condition_trigger::fill_full
            );
    id_insert( orderbook->insert_limit_order(false, beg , sz, ecb, aot2)); //1

    aot2 = AdvancedOrderTicketOTO::build_market(
            false, sz, condition_trigger::fill_full
            );
    id_insert( orderbook->insert_limit_order( false, conv(beg+incr), sz,
                                              ecb, aot2) ); //2

    aot = AdvancedOrderTicketOCO::build_limit(
            true, conv(beg+incr), sz*3, condition_trigger::fill_full
            );
    id_insert( orderbook->insert_limit_order(true, beg, sz*2, ecb, aot) ); //3
    /*
     *
     *  (3, B300)           (2, S100) --> (3, SM100) --> (3, SS100)
     *  (3, B200)   ->      (1, S100)------------------------|
     *
     *  100 100 100 100
     */
    orderbook->dump_buy_limits();
    orderbook->dump_sell_limits();
    orderbook->dump_buy_stops();
    orderbook->dump_sell_stops();

    if( orderbook->market_depth(nticks+2).size() > 0 ){
        return 14;
    }else if( orderbook->total_bid_size()
              || orderbook->total_ask_size() ){
        return 15;
    }

    if( orderbook->volume() != vol+100+200+400 ){
        return 16;
    }

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */

