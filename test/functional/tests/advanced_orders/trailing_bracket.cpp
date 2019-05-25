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

    condition_trigger FULL_FILL = condition_trigger::fill_full;
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

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 10, FULL_FILL);
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

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 10, FULL_FILL);
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

    auto aot = AdvancedOrderTicketTrailingBracket::build(1, 1, FULL_FILL);
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

    auto aot = AdvancedOrderTicketTrailingBracket::build(10, 1, FULL_FILL);
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

    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr, FULL_FILL);
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

int
TEST_advanced_TRAILING_BRACKET_6(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 5;
    size_t limit_incr = 5;

    ids.clear();

    try{
        auto aot = AdvancedOrderTicketTrailingBracket::build(1,1);
        orderbook->insert_limit_order(false, end, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 1;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto aot = AdvancedOrderTicketTrailingBracket::build(1,1);
        orderbook->insert_limit_order(true, end, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 2;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto n = orderbook->ticks_in_range(mid, end);
        auto aot = AdvancedOrderTicketTrailingBracket::build(n + 1, 1);
        orderbook->insert_stop_order(false, conv(mid+incr), mid, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 3;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto n = orderbook->ticks_in_range(mid, end);
        auto aot = AdvancedOrderTicketTrailingBracket::build(1, n + 1);
        orderbook->insert_stop_order(true, mid, conv(mid+incr), 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 3;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_limit_order(true, mid, sz*10, ecb, aot);
    orderbook->insert_market_order(false, sz, ecb);
    size_t exp_vol = sz;

    // mid +5  S 100 limit
    // ...
    // mid     B 900 limit
    // ..
    // mid -5  S 100 stop

    dump_orders(orderbook, out);

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t vol = orderbook->volume();
    if( tbs != sz*9 || tas != sz )
        return 1;
    if( vol != exp_vol )
        return 2;

    orderbook->insert_limit_order(true, conv(mid+incr), sz, ecb);
    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    // mid +5  S 100 limit
    // ...
    // mid     B 900 limit
    // ..
    // mid -4  S 100 stop

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    vol = orderbook->volume();
    if( bs != sz*9 )
        return 3;
    if( vol != exp_vol )
        return 4;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    // mid +5  S 200 limit
    // ...
    // mid     B 800 limit
    // ..
    // mid -4  S 200 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    vol = orderbook->volume();
    if( bs != sz*8 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    // mid +5  S 100 limit
    // ...
    // mid     B 800 limit S 100 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*8 || as != sz )
        return 7;
    if( vol != exp_vol )
        return 8;

    orderbook->insert_limit_order(true, beg, 10*sz, ecb);
    orderbook->insert_limit_order(false, end, sz, ecb);
    // this 1) hits bracket-entry, 2) adds to target/loss, 3) triggers loss
    // TODO test this more
    orderbook->insert_market_order(false, sz*8, ecb);
    exp_vol += sz*17;

    // end     S 100 limit
    // ...
    // ...
    // beg     B 100 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( bs != sz || as != sz || tbs != sz || tas != sz )
        return 9;
    if( vol != exp_vol )
        return 10;

    orderbook->insert_market_order(true, sz, ecb);
    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz*2;

    //

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( bs != 0 || as != 0 || tbs != 0 || tas != 0 )
        return 11;
    if( vol != exp_vol )
        return 12;

    return 0;
}

int
TEST_advanced_TRAILING_BRACKET_7(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 5;
    size_t limit_incr = 5;

    ids.clear();

    id_type id_primary, id_loss, id_target;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_BRACKET_open:
            id_primary = id1;
            id_target = id2;
            break;
        case callback_msg::trigger_BRACKET_open_target:
            if( id1 != id_primary )
                throw std::runtime_error("open-target: id1 != id_primary");
            if( id2 != id_target )
                throw std::runtime_error("open-target: id2 != id_target");
            break;
        case callback_msg::trigger_BRACKET_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            id_loss = id2;
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_limit_order(true, mid, sz*10, our_cb, aot);
    orderbook->insert_market_order(false, sz, ecb);
    size_t exp_vol = sz;

    // mid +5  S 100 limit
    // ...
    // mid     B 900 limit
    // ..
    // mid -5  S 100 stop

    dump_orders(orderbook, out);

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t vol = orderbook->volume();
    if( tbs != sz*9 || tas != sz )
        return 1;
    if( vol != exp_vol )
        return 2;

    auto oi = orderbook->get_order_info(id_loss);
    if( oi.is_buy )
        return 3;
    if( oi.stop != conv(mid-5*incr) )
        return 4;
    if( oi.size != sz )
        return 5;

    orderbook->insert_limit_order(true, conv(mid+4*incr), sz, ecb);
    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    // mid +5  S 100 limit
    // ...
    // mid     B 900 limit
    // mid -1  S 100 stop

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    vol = orderbook->volume();
    if( bs != sz*9 )
        return 6;
    if( vol != exp_vol )
        return 7;

    oi = orderbook->get_order_info(id_loss);
    if( oi.stop != conv(mid-incr) )
        return 8;
    if( oi.size != sz )
        return 9;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    // mid +5  S 200 limit
    // ...
    // mid     B 800 limit
    // mid -1  S 200 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    vol = orderbook->volume();
    if( bs != sz*8 )
        return 10;
    if( vol != exp_vol )
        return 11;

    oi = orderbook->get_order_info(id_loss);
    if( oi.size != sz*2 )
        return 12;

    orderbook->insert_limit_order(true, conv(mid-incr), sz*11, ecb);
    orderbook->insert_market_order(false, sz*8, ecb);
    exp_vol += 8*sz;

    // mid +5  S 1000 limit
    // ...
    // mid -1  S 1000 stop    B 1100 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    vol = orderbook->volume();
    size_t as = orderbook->ask_size();
    if( bs != sz*11 || as != sz*10 )
        return 13;
    if( vol != exp_vol )
        return 14;

    oi = orderbook->get_order_info(id_loss);
    if( oi.size != sz*10 )
        return 15;

    oi = orderbook->get_order_info(id_target);
    if( oi.size != sz *10 )
        return 16;
    if( oi.limit != conv(mid + 5*incr))
        return 17;

    if( orderbook->get_order_info(id_primary) )
        return 18;

    orderbook->insert_limit_order(true, beg, sz, ecb);
    orderbook->insert_limit_order(false, end, sz, ecb);

    // end     S 100 limit
    // ...
    // mid +5  S 1000 limit
    // ...
    // mid -1  S 1000 stop    B 1100 limit
    // ...
    // beg     B 100 limit

    dump_orders(orderbook, out);

    orderbook->insert_market_order( false, sz*2, ecb);
    exp_vol += sz*12;

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( bs != 0 || as != 0 || tbs != 0 || tas != 0 )
        return 19;
    if( vol != exp_vol )
        return 10;

    if( orderbook->get_order_info(id_target) )
        return 21;

    if( orderbook->get_order_info(id_loss) )
        return 22;


    return 0;
}

int
TEST_advanced_TRAILING_BRACKET_8(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 5;
    size_t limit_incr = 5;

    ids.clear();

    id_type id_primary, id_loss, id_target;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_BRACKET_open:
            id_primary = id1;
            id_target = id2;
            break;
        case callback_msg::trigger_BRACKET_open_target:
            if( id1 != id_primary )
                throw std::runtime_error("open-target: id1 != id_primary");
            if( id2 != id_target )
                throw std::runtime_error("open-target: id2 != id_target");
            break;
        case callback_msg::trigger_BRACKET_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            id_loss = id2;
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_limit_order(false, mid, sz*10, our_cb, aot);
    orderbook->insert_market_order(true, sz, ecb);
    size_t exp_vol = sz;

    dump_orders(orderbook, out);

    // mid + 5     B 100 stop
    // ...
    // mid         S 900 limit
    // ...
    // mid -5      B 100 limit

    if( !orderbook->pull_order(id_primary) )
        return 1;

    // mid + 5     B 100 stop
    // ...
    // mid -5      B 100 limit

    orderbook->insert_limit_order(false, conv(mid-4*incr), sz, ecb);
    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook, out);

    // mid + 1     B 100 stop
    // ...
    // mid -5      B 100 limit

    auto oi = orderbook->get_order_info(id_loss);
    if( !oi.is_buy )
        return 2;
    if( oi.stop != conv(mid+incr) )
        return 3;
    if( oi.size != sz )
        return 4;

    oi = orderbook->get_order_info(id_target);
    if( !oi.is_buy )
        return 5;
    if( oi.limit != conv(mid-5*incr) )
        return 6;
    if( oi.size != sz )
        return 7;

    orderbook->insert_limit_order(false, end, sz, ecb);
    orderbook->insert_market_order(false, sz/2, ecb);
    exp_vol += sz/2;

    dump_orders(orderbook, out);

    // end         S 100 limit
    // ...
    // mid         B 50 stop
    // ...
    // mid - 5     B 50 limit

    oi = orderbook->get_order_info(id_loss);
    if( oi.stop != mid )
        return 8;
    if( oi.size != sz/2 )
        return 9;

    oi = orderbook->get_order_info(id_target);
    if( oi.limit != conv(mid-5*incr) )
        return 10;
    if( oi.size != sz/2 )
        return 11;

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t vol = orderbook->volume();
    if( tbs != sz/2 || tas != sz )
        return 12;
    if( vol != exp_vol )
        return 13;

    orderbook->insert_market_order(true, sz/2, ecb);
    exp_vol += sz;

    //

    dump_orders(orderbook, out);

    if( orderbook->get_order_info(id_loss) )
        return 14;
    if( orderbook->get_order_info(id_target) )
        return 15;
    if( orderbook->get_order_info(id_primary) )
        return 16;

    size_t ts = orderbook->total_size();
    vol = orderbook->volume();
    if( ts != 0 )
        return 17;
    if( vol != exp_vol )
        return 18;

    return 0;
}

int
TEST_advanced_TRAILING_BRACKET_9(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 5;
    size_t limit_incr = 5;

    ids.clear();

    id_type id_primary, id_loss, id_target;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_BRACKET_open:
            id_primary = id1;
            id_target = id2;
            break;
        case callback_msg::trigger_BRACKET_open_target:
            if( id1 != id_primary )
                throw std::runtime_error("open-target: id1 != id_primary");
            if( id2 != id_target )
                throw std::runtime_error("open-target: id2 != id_target");
            break;
        case callback_msg::trigger_BRACKET_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            id_loss = id2;
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    orderbook->insert_limit_order(true, mid, sz, ecb);
    orderbook->insert_limit_order(true, conv(mid+incr), sz*2, ecb);


    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_stop_order(false, conv(mid+incr), conv(mid),
                                        sz*10, our_cb, aot);


    // mid +1    B 200 limit   S 1000 stop (@ mid)
    // mid       B 100 limit

    orderbook->insert_market_order(false, sz, ecb);
    size_t exp_vol = sz*3;

    // mid +5    B 200 stop
    // ...
    // mid       S 800 limit
    // ..
    // mid - 5   B 200 limit

    dump_orders(orderbook, out);

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t vol = orderbook->volume();
    if( tbs != sz*2 || tas != sz*8 )
        return 1;
    if( vol != exp_vol )
        return 2;

    auto oi = orderbook->get_order_info(id_loss);
    if( !oi.is_buy )
        return 3;
    if( oi.size != sz*2 )
        return 4;
    if( oi.stop != conv(mid+5*incr) )
        return 5;

    oi = orderbook->get_order_info(id_target);
    if( !oi.is_buy )
        return 6;
    if( oi.size != sz*2 )
        return 7;
    if( oi.limit != conv(mid-5*incr) )
        return 8;

    if( !orderbook->pull_order(id_loss) )
        return 9;

    // mid       S 800 limit

    dump_orders(orderbook, out);

    orderbook->insert_market_order(true, sz*2, ecb);
    exp_vol += sz*2;

    // mid +5    B 200 stop
    // ...
    // mid       S 600 limit
    // ..
    // mid - 5   B 200 limit

    dump_orders(orderbook, out);

    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( tbs != sz*2 || tas != sz*6 )
        return 10;
    if( vol != exp_vol )
        return 11;

    orderbook->insert_limit_order(false, conv(mid-4*incr), sz*2, ecb);
    orderbook->insert_stop_order(true, conv(mid-4*incr), sz*2, ecb);
    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz*3;

     // mid + 1    B 300 stop
     // mid        S 500 limit
     // ..
     // mid - 5    B 300 limit

     dump_orders(orderbook, out);

     tbs = orderbook->total_bid_size();
     tas = orderbook->total_ask_size();
     vol = orderbook->volume();
     if( tbs != sz*3 || tas != sz*5 )
         return 12;
     if( vol != exp_vol )
         return 13;

     oi = orderbook->get_order_info(id_loss);
     if( !oi.is_buy )
         return 14;
     if( oi.size != sz*3 )
         return 15;
     if( oi.stop != conv(mid+incr) )
         return 16;

     oi = orderbook->get_order_info(id_target);
     if( !oi.is_buy )
         return 17;
     if( oi.size != sz*3 )
         return 18;
     if( oi.limit != conv(mid-5*incr) )
         return 19;

     if( !orderbook->pull_order(id_target) )
         return 20;

     if( orderbook->get_order_info(id_loss) )
         return 21;

     // mid        S 500 limit

     orderbook->insert_limit_order(true, conv(mid+4*incr), sz*6, ecb);
     exp_vol += sz*5;

     // mid +5    B 500 stop
     // mid + 4   B 100 limit
     // ..
     // mid - 5   B 500 limit

     for(double i = conv(mid+3*incr); i > conv(mid-5*incr); i = conv(i-incr)){
         orderbook->insert_limit_order(true, conv(i), sz, ecb);
     }

     // mid +5    B 500 stop   (loss )
     // mid + 4   B 100 limit
     // mid + 3   B 100 limit
     // mid + 2   B 100 limit
     // mid + 1   B 100 limit
     // mid       B 100 limit
     // mid - 1   B 100 limit
     // mid - 2   B 100 limit
     // mid - 3   B 100 limit
     // mid - 4   B 100 limit
     // mid - 5   B 500 limit (target)

     dump_orders(orderbook, out);

     tbs = orderbook->total_bid_size();
     tas = orderbook->total_ask_size();
     vol = orderbook->volume();
     if( tbs != sz*14|| tas != 0 )
         return 22;
     if( vol != exp_vol )
         return 23;

     orderbook->insert_market_order(false, sz*10, ecb);
     exp_vol += sz*10;

     // mid    B 400 stop   (loss )
     // ...
     // mid - 5   B 400 limit (target)

     dump_orders(orderbook, out);

     tbs = orderbook->total_bid_size();
     tas = orderbook->total_ask_size();
     vol = orderbook->volume();
     if( tbs != sz*4|| tas != 0 )
         return 23;
     if( vol != exp_vol )
         return 24;

     orderbook->insert_limit_order(false, end, sz*5, ecb);
     orderbook->insert_limit_order(true, beg, sz, ecb);
     orderbook->insert_market_order(true, sz, ecb);
     exp_vol += sz*5;

     orderbook->insert_market_order(false, sz, ecb);
     exp_vol += sz;

     tbs = orderbook->total_bid_size();
     tas = orderbook->total_ask_size();
     vol = orderbook->volume();
     if( tbs != 0|| tas != 0 )
         return 25;
     if( vol != exp_vol )
         return 26;

    return 0;
}


int
TEST_advanced_TRAILING_BRACKET_10(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();
    size_t stop_incr = 5;
    size_t limit_incr = 5;

    ids.clear();

    id_type id_primary, id_loss, id_target;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_BRACKET_open:
            id_primary = id1;
            id_target = id2;
            break;
        case callback_msg::trigger_BRACKET_open_target:
            if( id1 != id_primary )
                throw std::runtime_error("open-target: id1 != id_primary");
            if( id2 != id_target )
                throw std::runtime_error("open-target: id2 != id_target");
            break;
        case callback_msg::trigger_BRACKET_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            id_loss = id2;
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    for(int i = 0; i < 5; ++i){
        orderbook->insert_limit_order(false, conv(mid+i*incr), sz, ecb);
    }


    auto aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_stop_order(true, mid, sz*4, our_cb, aot);

    // mid + 4   S 100 limit
    // mid + 3   S 100 limit
    // mid + 2   S 100 limit
    // mid + 1   S 100 limit
    // mid       S 100 limit   B 400 stop

    dump_orders(orderbook, out);

    orderbook->insert_market_order(true, sz, ecb);
    size_t exp_vol = 5*sz;

    // mid + 9    S 400 limit
    // ...
    // mid - 1    S 400 stop

    dump_orders(orderbook, out);

    double ap = orderbook->ask_price();
    size_t vol = orderbook->volume();
    if( ap != conv(mid+9*incr) )
        return 1;
    if( vol != exp_vol )
        return 2;

    auto oi = orderbook->get_order_info(id_loss);
    if( oi.is_buy )
        return 3;
    if( oi.size != sz*4 )
        return 4;
    if( oi.stop != conv(mid-incr) )
        return 5;

    oi = orderbook->get_order_info(id_target);
    if( oi.is_buy )
        return 6;
    if( oi.size != sz*4 )
        return 7;
    if( oi.limit != conv(mid+9*incr) )
        return 8;

    orderbook->insert_limit_order(true, mid, sz, ecb);
    orderbook->insert_limit_order(true, conv(mid-incr), sz*5, ecb);

    // mid + 9    S 400 limit
    // ...
    //            B 100 limit
    // mid - 1    S 400 stop  B 500 limit

    dump_orders(orderbook, out);

    orderbook->insert_limit_order(false, mid, sz, ecb);
    exp_vol += sz;

    double bp = orderbook->bid_price();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*5 || as != sz*4 )
        return 9;
    if( bp != conv(mid-incr) )
        return 10;
    if( vol != exp_vol )
        return 11;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += 5*sz;

    //

    dump_orders(orderbook, out);

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0 || as != 0 )
        return 12;
    if( bp != 0 )
        return 13;
    if( vol != exp_vol )
        return 14;

    /***/

    for(int i = 0; i < 5; ++i){
        orderbook->insert_limit_order(false, conv(mid+i*incr), sz, ecb);
    }


    aot = AdvancedOrderTicketTrailingBracket::build(stop_incr, limit_incr);
    orderbook->insert_stop_order(true, mid,conv(mid+4*incr),sz*6, our_cb, aot);

    // mid + 4   S 100 limit
    // mid + 3   S 100 limit
    // mid + 2   S 100 limit
    // mid + 1   S 100 limit
    // mid       S 100 limit   B 600 stop

    dump_orders(orderbook, out);

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += 5*sz;

    // mid + 9    S 400 limit
    // ...
    // mid + 4    B 200 limit
    // ...
    // mid - 1    S 400 stop

    dump_orders(orderbook, out);

    orderbook->insert_limit_order(true, conv(mid+5*incr), sz, ecb);
    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += 2*sz;

    // mid + 9    S 500 limit
    // ...
    // mid + 4    B 100 limit
    // ...
    // mid        S 500 stop

    dump_orders(orderbook, out);

    id_type id_loss_old = id_loss;
    id_type id_target_old = id_target;

    if( !orderbook->replace_with_limit_order(id_primary, true, conv(mid+4*incr),
                                            sz*2, our_cb, aot) )
        return 15;

    // mid + 9    S 500 limit
    // ...
    // mid + 4    B 200 limit
    // ...
    // mid        S 500 stop

    dump_orders(orderbook, out);

    orderbook->insert_market_order(false, sz);
    exp_vol += sz;

    // mid + 9    S 500 limit S 100 limit
    // ...
    // mid + 4    B 100 limit
    // ...
    // mid        S 500 stop
    // mid -1     S 100 stop

    dump_orders(orderbook, out);

    ap = orderbook->ask_price();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( as != sz* 6 || ap != conv(mid+9*incr) )
        return 16;
    if( vol != exp_vol )
        return 17;

    oi = orderbook->get_order_info(id_loss_old);
    if( oi.is_buy )
        return 18;
    if( oi.size != sz*5 )
        return 19;
    if( oi.stop != conv(mid) )
        return 20;

    oi = orderbook->get_order_info(id_target_old);
    if( oi.is_buy )
        return 21;
    if( oi.size != sz*5 )
        return 22;
    if( oi.limit != conv(mid+9*incr) )
        return 23;

    oi = orderbook->get_order_info(id_loss);
    if( oi.is_buy )
        return 24;
    if( oi.size != sz)
        return 25;
    if( oi.stop != conv(mid-incr) )
        return 26;

    oi = orderbook->get_order_info(id_target);
    if( oi.is_buy )
        return 27;
    if( oi.size != sz )
        return 28;
    if( oi.limit != conv(mid+9*incr) )
        return 29;

    if( !orderbook->pull_order(id_primary) )
        return 30;

    // mid + 9    S 500 limit S 100 limit
    // ...
    // mid        S 500 stop
    // mid -1     S 100 stop

    dump_orders(orderbook, out);


    orderbook->insert_limit_order(true, conv(mid+incr), sz, ecb);
    orderbook->insert_limit_order(true, conv(mid-incr), sz*7, ecb);

    // mid + 9    S 500 limit S 100 limit
    // ...
    // mid + 1    B 100 limit
    // mid        S 500 stop
    // mid - 1    S 100 stop   B 700 limit

    dump_orders(orderbook, out);

    orderbook->insert_limit_order(false, conv(mid+incr), sz, ecb);
    exp_vol += sz;

    // mid + 9    S 500 limit S 100 limit
    // ...
    // mid        S 500 stop
    // mid - 1    S 100 stop  B 700 limit

    dump_orders(orderbook, out);

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*7 || as != sz*6 )
        return 31;
    if( bp != conv(mid-incr) )
        return 32;
    if( vol != exp_vol )
        return 33;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += 7*sz;

    //

    dump_orders(orderbook, out);

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0 || as != 0 )
        return 34;
    if( bp != 0 )
        return 35;
    if( vol != exp_vol )
        return 36;

    return 0;

}

#endif /* RUN_FUNCTIONAL_TESTS */


