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
TEST_advanced_TRAILING_STOP_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketTrailingStop::build(10);
    id_type id1 = orderbook->insert_limit_order(true, mid, sz, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz, ecb);
    orderbook->insert_limit_order(false, conv(mid+3*incr), sz, ecb);
    orderbook->insert_market_order(false, sz);
    dump_orders(orderbook,out);

    order_info oi = orderbook->get_order_info(ids[id1]);
    out<< "ORDER INFO: " << ids[id1] << " " << oi << endl;
    if( oi.stop != conv(mid-10*incr) ){
        return 1;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop != conv(mid-9*incr) ){
        return 2;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop != conv(mid-7*incr) ){
        return 3;
    }

    return 0;
}


int
TEST_advanced_TRAILING_STOP_2(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;

    ids.clear();

    for(double d = conv(mid+incr); d <= end; d=conv(d+incr) ){
        orderbook->insert_limit_order(false, d, sz, ecb);
    }

    for(double d = conv(mid-incr); d >= beg; d=conv(d-incr) ){
        orderbook->insert_limit_order(true, d, sz, ecb);
    }

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr);
    id_type id1 = orderbook->insert_limit_order(false, mid, sz, ecb, aot);
    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);

    order_info oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop ){
        return 1;
    }

    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);
    oi = orderbook->get_order_info(ids[id1]);
    double exp_stop = conv(mid+stop_incr*incr);
    if( oi.stop != exp_stop ){
        return 1;
    }
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
        orderbook->insert_market_order(bool(i >= 0), sz*abs(i) , ecb);
        orderbook->dump_stops(out);

        max_neg_price = min(orderbook->last_price(), max_neg_price);
        oi = orderbook->get_order_info(ids[id1]);
        exp_stop = conv(max_neg_price+stop_incr*incr);
        if( oi.stop != exp_stop){
            return 3;
        }
    }
    orderbook->dump_stops(out);

    if( oi.stop != mid ){
        return 4;
    }

    orderbook->insert_limit_order(false, mid, sz, ecb);
    orderbook->insert_market_order(true, sz);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi ){
        return 5;
    }

    if( orderbook->last_price() != conv(mid+6*incr)){
        return 6;
    }

    return 0;
}

int
TEST_advanced_TRAILING_STOP_3(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    unsigned long long vol = 0;

    ids.clear();

    orderbook->insert_limit_order(true, beg, sz, ecb);
    orderbook->insert_limit_order(true, conv(beg+incr), sz*5, ecb);
    orderbook->insert_limit_order(false, end, sz*5, ecb);

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr);
    orderbook->insert_limit_order(true, mid, sz*2, ecb, aot);
    id_type id2 = orderbook->insert_stop_order(false, mid, sz, ecb, aot);
    //
    //  mid    200    S 100
    //  ..
    //  beg    500
    dump_orders(orderbook,out);

    // issues both trailing stops, but only the first trades
    orderbook->insert_limit_order(false, beg, sz*2, ecb);
    vol += 5*sz;
    // ...
    //  mid-10
    //  ..
    //  beg+10  S 100(c)
    //  ..
    //  beg     L 200(b)
    dump_orders(orderbook,out);

    if( orderbook->last_price() != conv(beg+incr) ){
        return 1;
    }else if(orderbook->volume() != vol ){
        return 2;
    }

    //
    //  beg+10   S 100(c)
    //  beg + 1  L 100
    //  beg      L 100

    if( orderbook->last_price() != conv(beg+incr) ){
        return 3;
    }else if( orderbook->volume() != vol ){
        return 4;
    }else if( orderbook->total_ask_size() != 5*sz ){
        return 5;
    }else if( orderbook->total_bid_size() != 3*sz ){
        return 6;
    }

    auto ts = orderbook->time_and_sales();
    if( ts.size() != 3 ){
        return 7;
    }
    if( get<2>(ts[2]) != sz*2){
        return 8;
    }else if( get<2>(ts[1]) != sz ){
        return 9;
    }else if( get<2>(ts[0]) != sz*2 ){
        return 10;
    }

    order_info oi = orderbook->get_order_info(ids[id2]);
    if( !oi || oi.stop != conv(beg + incr + stop_incr * incr) ){
        return 11;
    }

    orderbook->insert_limit_order(false, beg, sz*3, ecb);
    vol += 3 * sz;
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id2]);
    if( oi.stop != conv(beg + stop_incr * incr) ){
        return 12;
    }

    if( !orderbook->pull_order(ids[id2]) ){
        return 13;
    }
    dump_orders(orderbook,out);


    orderbook->insert_limit_order(true, end, sz);
    vol += sz;
    if( orderbook->volume() != vol ){
        return 14;
    }

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */


