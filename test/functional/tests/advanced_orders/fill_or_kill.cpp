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
#include <vector>
#include <tuple>
#include <iostream>

using namespace sob;
using namespace std;

namespace {
    size_t sz = 100;
}

int
TEST_advanced_FOK_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    set<id_type> kill_ids;
    set<id_type> fok_ids;
    set<id_type> ids;
    auto ecb = [&]( callback_msg msg, id_type id1, id_type id2,
                    double price, size_t size)
        {
            if(msg == callback_msg::trigger_OCO  ){
                ids.erase(id1);
                ids.insert(id2);
            }
            if(msg == callback_msg::kill ){
                kill_ids.insert(id1);
            }
            callback(msg,id1,id2,price,size);
        };

    auto aot = AdvancedOrderTicketFOK::build();
    out<< " FOK (full buy, empty book) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(true, end, sz, ecb, aot) );

    aot.change_trigger(condition_trigger::fill_partial);
    out<< " FOK (partial sell, empty book) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(false, beg, sz, ecb, aot) );

    aot.change_trigger(condition_trigger::fill_full);
    ids.insert( orderbook->insert_limit_order(true, conv(mid-1*incr), sz, ecb));
    ids.insert( orderbook->insert_limit_order(true, conv(mid-2*incr), sz*2, ecb));
    ids.insert( orderbook->insert_limit_order(false, conv(mid+1*incr), sz, ecb));
    ids.insert( orderbook->insert_limit_order(true, conv(mid-3*incr), sz*3, ecb));
    ids.insert( orderbook->insert_limit_order(false, conv(mid+2*incr), sz*2, ecb));
    ids.insert( orderbook->insert_limit_order(false, conv(mid+3*incr), sz*3, ecb));
    orderbook->dump_limits(out);

    out<< " FOK (full buy) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(true, mid, sz, ecb, aot) );

    out<< " FOK (full sell) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(false, mid, sz, ecb, aot) );

    aot.change_trigger(condition_trigger::fill_partial);
    out<< " FOK (partial buy) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(true, mid, sz, ecb, aot) );

    out<< " FOK (partial sell) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order(false, mid, sz, ecb, aot) );

    aot.change_trigger(condition_trigger::fill_full);
    out<< " FOK (full buy +) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order( true, conv(mid+incr), sz+1,
                                                   ecb, aot) );

    out<< " FOK (full sell +) - should kill" << endl;
    fok_ids.insert( orderbook->insert_limit_order( false, conv(mid-incr), sz+1,
                                                   ecb, aot) );

    orderbook->dump_limits(out);

    if( kill_ids != fok_ids ){
        out<< "ERROR(1) FOK kill IDs don't match: " << endl;
        for(id_type i : kill_ids){
            out<< i << " ";
        }
        out<< endl;
        for(id_type i : fok_ids){
            out<< i << " ";
        }
        out<< endl;
        return 1;
    }
    fok_ids.clear();

    out<< " FOK (full buy ++) - should fill" << endl;
    fok_ids.insert( orderbook->insert_limit_order( true, conv(mid+2*incr),
                                                   sz*3-1, ecb, aot) );
    orderbook->dump_sell_limits(out);

    out<< " FOK (full sell ++) - should fill" << endl;
    fok_ids.insert( orderbook->insert_limit_order( false, conv(mid-2*incr),
                                                   sz*3-1, ecb, aot) );
    orderbook->dump_buy_limits(out);

    ids.insert( orderbook->insert_stop_order( true, conv(mid+2*incr),
                                              conv(mid+3*incr), sz, ecb) );

    aot.change_trigger(condition_trigger::fill_partial);
    out<< " FOK (partial buy +++) - should fill" << endl;
    fok_ids.insert( orderbook->insert_limit_order( true, conv(mid+4*incr),
                                                   sz*4+1, ecb, aot) );
    orderbook->dump_limits(out);

    out<< " FOK (partial sell +++) - should fill" << endl;
    id_type fok_id_last =  orderbook->insert_limit_order( false, conv(mid-4*incr),
                                                          sz*6+1, ecb, aot );

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();

    if( bp != 0 ){
        return 2;
    }else if( ap != conv(mid-4*incr) ){
        return 3;
    }else if( bs != 0 ){
        return 4;
    }else if( as != sz ){
        return 5;
    }else if( vol != sz * (6+3+5) ){
        return 6;
    }

    auto timesales = orderbook->time_and_sales();
    if( timesales.size() != 10 ){
        return 7;
    }

    double p_2nd_last = get<1>( timesales[timesales.size()-2] );
    size_t sz_2nd_last = get<2>( timesales[timesales.size()-2] );
    if( p_2nd_last != conv(mid-2*incr) ){
        return 8;
    }else if( sz_2nd_last != 1 ){
        return 9;
    }

    for(id_type i : fok_ids){
        if( orderbook->pull_order(i) ){
            out<< "order should not have been pulled: " << i << endl;
            return 10;
        }
    }

    if( !orderbook->pull_order(fok_id_last) ){
        out<<"failed to remoke FOK order: " << fok_id_last << endl;
        return 11;
    }

    if( orderbook->market_depth(orderbook->ticks_in_range()+2).size() ){
        return 12;
    }

    return 0;
};

#endif /* RUN_FUNCTIONAL_TESTS */


