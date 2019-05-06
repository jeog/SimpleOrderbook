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

#include <map>
#include <vector>
#include <tuple>
#include <iostream>

using namespace sob;
using namespace std;

namespace {
    map<id_type, id_type> ids;
    auto ecb = create_advanced_callback(ids);
    size_t sz = 100;

    inline void
    id_insert(id_type id)
    { ids[id] = id; }
}

int
TEST_advanced_TRAILING_BRACKET_1(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(true, beg, 10*sz, ecb);
    orderbook->insert_limit_order(false, end, 10*sz, ecb);

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 10);
    out<< "OrderParamaters 1: " << *aot.order1() << endl;
    out<< "OrderParamaters 2: " << *aot.order2() << endl;
    id_type id3 = orderbook->insert_limit_order(true, mid, sz, ecb, aot);

    order_info oi = orderbook->get_order_info(id3);
    out<< "ORDER INFO: " << id3 << " " << oi << endl;

    orderbook->insert_market_order(false, sz); // vol 100
    dump_orders(orderbook, out);

    oi = orderbook->get_order_info(ids[id3]);
    out<< "ORDER INFO: " << ids[id3] << " " << oi << endl;
    if( oi.limit != conv(mid+10*incr) ){
        return 1;
    }

    orderbook->insert_limit_order(false, conv(mid+incr), sz);
    orderbook->insert_limit_order(true,  conv(mid+2*incr), sz*2); //vol 200
    // trailing stop + 1
    dump_orders(orderbook,out);

    orderbook->insert_limit_order(false, conv(mid + 2*incr), sz); // vol 300
    // trailing stop + 1
    dump_orders(orderbook,out);

    if( orderbook->volume() != 3*sz ){
        return 2;
    }else if( orderbook->ask_price() != conv(mid+10*incr) ){
        return 3;
    }

    orderbook->insert_limit_order(false, conv(mid-8*incr), sz);
    orderbook->insert_market_order(true, sz); // vol 400 -> 500
    // should hit trailing stop
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id3]);
    if( oi ){
        return 4;
    }

    if( orderbook->volume() != 5*sz ){
        return 5;
    }else if( orderbook->last_price() != beg ){
        return 6;
    }

    return 0;
}


int
TEST_advanced_TRAILING_BRACKET_2(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(true, beg, 10*sz, ecb);
    orderbook->insert_limit_order(false, end, 10*sz, ecb);

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 10);
    id_type id3 = orderbook->insert_limit_order(false, mid, sz, ecb, aot);
    orderbook->insert_market_order(true, sz); // vol 100
    dump_orders(orderbook,out);

    order_info oi = orderbook->get_order_info(ids[id3]);
    if( oi.limit != conv(mid-10*incr) ){
        return 1;
    }

    orderbook->insert_limit_order(true, conv(mid-incr), sz);
    orderbook->insert_limit_order(false,  conv(mid-9*incr), sz*2); //vol 200
    // trailing stop - 1
    dump_orders(orderbook,out);

    orderbook->insert_limit_order(true, conv(mid-9*incr), sz); // vol 300
    // trailing stop - 8
    dump_orders(orderbook,out);

    if( orderbook->volume() != 3*sz ){
        return 2;
    }else if( orderbook->bid_price() != conv(mid-10*incr) ){
        return 3;
    }

    orderbook->insert_limit_order(false, conv(mid-10*incr), sz); // vol 400
    // should hit limit
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id3]);
    if( oi ){
        return 4;
    }

    if( orderbook->volume() != 4*sz ){
        return 5;
    }else if( orderbook->last_price() != conv(mid-10*incr) ){
        return 6;
    }

    return 0;
}

int
TEST_advanced_TRAILING_BRACKET_3(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(false, mid, sz*2, ecb);

    auto aot = AdvancedOrderTicketTrailingBracket::build(1, 1);
    id_type id2 = orderbook->insert_stop_order(true, mid, sz, ecb, aot);
    ids[id2] = id2;
    orderbook->insert_stop_order(true, mid, conv(mid+incr), sz , ecb);
    dump_orders(orderbook,out);

    order_info oi = orderbook->get_order_info(ids[id2]);
    if( oi.advanced.condition() != order_condition::trailing_bracket ){
        return 1;
    }else if( oi.stop != mid ){
        return 2;
    }

    // should trigger stop and hit target
    // 100 100 100
    orderbook->insert_limit_order(true, mid, sz, ecb);
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id2]);
    if( oi ){
        return 3;
    }

    auto ts = orderbook->time_and_sales();
    if( ts.size() != 3 ){
        return 4;
    }else if( get<1>(ts[0]) != mid ){
        return 5;
    }else if( get<1>(ts[1]) != mid ){
        return 6;
    }else if( get<1>(ts[2]) != conv(mid + incr) ){
        return 7;
    }

    return 0;
}


int
TEST_advanced_TRAILING_BRACKET_4(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(true, mid, sz*2, ecb);
    orderbook->insert_stop_order(false, mid, conv(mid-10*incr), sz*2, ecb);

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 1);
    id_type id2 = orderbook->insert_market_order(false, sz, ecb, aot);
    ids[id2] = id2;

    // 100 100 100
    dump_orders(orderbook,out);

    order_info oi = orderbook->get_order_info(ids[id2]);
    if( oi ){
        return 1;
    }

    auto ts = orderbook->time_and_sales();
    if( ts.size() != 3 ){
        return 2;
    }else if( get<1>(ts[0]) != mid ){
        return 3;
    }else if( get<1>(ts[1]) != mid ){
        return 4;
    }else if( get<1>(ts[2]) != conv(mid-10*incr) ){
        return 5;
    }

    return 0;
}


int
TEST_advanced_TRAILING_BRACKET_5(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 10;
    size_t limit_incr = 10;

    ids.clear();

    for(double d = conv(mid+incr); d <= end; d=conv(d+incr) ){
        orderbook->insert_limit_order(false, d, sz, ecb);
    }

    for(double d = conv(mid-incr); d >= beg; d=conv(d-incr) ){
        orderbook->insert_limit_order(true, d, sz, ecb);
    }

    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    id_type id1 = orderbook->insert_limit_order(false, mid, sz, ecb, aot);
    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);

    order_info oi = orderbook->get_order_info(ids[id1]);
    if( oi.advanced.condition() != order_condition::trailing_bracket ){
        return 1;
    }else if( oi.advanced.trigger() != condition_trigger::fill_full ){
        return 2;
    }else if( oi.stop ){
        return 3;
    }else if( oi.limit != mid ){
        return 4;
    }

    double exp_stop = conv(mid + stop_incr * incr);

    const OrderParamaters *op1 = oi.advanced.order1();
    if( !op1->is_by_nticks() || !op1->is_stop_order() ){
        return 5;
    }else if( !op1->is_buy() ){
        return 6;
    }else if( op1->size() != sz ){
        return 7;
    }else if( op1->stop_nticks() != stop_incr ){
        return 8;
    }

    const OrderParamaters *op2 = oi.advanced.order2();
    if( !op2->is_by_nticks() || !op2->is_limit_order() ){
        return 9;
    }else if( !op2->is_buy() ){
        return 10;
    }else if( op2->size() != sz ){
        return 11;
    }else if( op2->limit_nticks() != limit_incr ){
        return 12;
    }

    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);
    //
    //                mid + 5 (#5)
    //                mid + 2 (#4)
    //                mid + 1 (#1)
    // mid - 1 (#2)
    // mid - 2
    // mid - 4 (#3)
    // mid - 10 (#6)
    //

    double max_neg_price = mid;
    for(int i : {1,-1,-3, 1, 3, -6}){ // need pos to stat > -10
        orderbook->insert_market_order(bool(i >= 0), sz*abs(i), ecb);
        orderbook->dump_stops(out);

        max_neg_price = min(orderbook->last_price(), max_neg_price);
        oi = orderbook->get_order_info(ids[id1]);
        if( oi.advanced.condition() != order_condition::_trailing_bracket_active){
            return 13;
        }

        exp_stop = conv(max_neg_price + stop_incr * incr);
        if( oi.advanced.order1()->stop_price() != exp_stop ){
            return 14;
        }
    }

    orderbook->dump_stops(out);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi.advanced.order1()->stop_price() != mid ){
        return 15;
    }

    orderbook->insert_limit_order(false, mid, sz, ecb);
    orderbook->insert_market_order(true, sz);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi ){
        return 16;
    }

    if( orderbook->last_price() != conv(mid + 6*incr)){
        return 17;
    }

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */


