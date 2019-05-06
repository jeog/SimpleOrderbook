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

using namespace std;
using namespace sob;

namespace{
    size_t sz = 100;

    set<id_type> ids;
    auto ecb = [&]( sob::callback_msg msg, sob::id_type id1, sob::id_type id2,
                    double price, size_t size)
        {
            if(msg == callback_msg::trigger_OTO ){
                ids.insert(id2);
            }
            callback(msg,id1,id2,price,size);
        };
}

int
TEST_advanced_OTO_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    size_t nticks = orderbook->ticks_in_range();

    int n = static_cast<int>(nticks/3);
    if( n <= 4 ){
        return 1;
    }

    for(int i = 0; i < n; ++ i ){
        auto aot_buy = AdvancedOrderTicketOTO::build_limit(true, end, sz);
        auto aot_sell = AdvancedOrderTicketOTO::build_market(false, sz);
        ids.insert( orderbook->insert_limit_order( true, conv(beg+i*incr),
                                                   (i ? sz : (2*sz)), ecb,
                                                   aot_buy ) );
        id_type id = orderbook->insert_limit_order( false, conv(end-i*incr),
                                                    (i ? sz : (2*sz)), ecb,
                                                    aot_sell );
        ids.insert(id);

        order_info of = orderbook->get_order_info(id);
        if( of.limit != conv(end-i*incr)
            || of.is_buy
            || of.size != (i ? sz : (2*sz))
            || of.type != order_type::limit
            || of.advanced.condition() != order_condition::one_triggers_other )
        {
            return 2;
        }

        const OrderParamaters *op = of.advanced.order1();
        if( !op->is_by_price()
            || op->get_order_type() != order_type::market
            || !op->is_market_order()
            || op->size() != sz )
        {
            return 3;
        }
        out<< "ORDER INFO: " << id << " "<< of << endl;
    }

    /*
     *                      end     200 (buy 100)
     *                      end - 1 100 (buy 100)
     *                      end - 2 100 (buy 100)
     *                      ...
     *
     *                      ...
     *  beg + 2  100 <-- (sell 100)
     *  beg + 1  100 (sell 100)
     *  beg      200 (sell 100) (sell 100)
     */
    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);

    orderbook->insert_market_order(false, sz);
    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);

    double bp = orderbook->bid_price();
    size_t tbp = orderbook->total_bid_size();
    size_t tap = orderbook->total_ask_size();
    double ap = orderbook->ask_price();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();

    if( bp != 0 ){
        return 4;
    }else if( ap != end ){
        return 5;
    }else if( bs != 0 ){
        return 6;
    }else if( as != sz ){
        return 7;
    }else if( tbp != 0 ){
        return 8;
    }else if( tap != sz ){
        return 9;
    }else if( vol != sz * (2*n+1) ){
        return 10;
    }

    auto md_book = orderbook->market_depth(nticks + 2);
    size_t book_sz = md_book.size();
    if( book_sz != 1 ){
        return 11;
    }

    out<< "pulls: " << boolalpha <<endl;
    for(auto id : ids ){
        if( orderbook->pull_order(id) ){
            out<< id << " ";
        }
    }
    out<< endl;
    ids.clear();

    md_book = orderbook->market_depth(nticks + 2);
    book_sz = md_book.size();
    if( book_sz > 0 ){
        return 12;
    }else if( orderbook->total_bid_size() || orderbook->total_ask_size() ){
        return 13;
    }

    return 0;
}



int
TEST_advanced_OTO_2(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    size_t nticks = orderbook->ticks_in_range();

    ids.clear();

    auto aot_s1 = AdvancedOrderTicketOTO::build_stop(true, conv(end-3*incr), sz); //
    auto aot_sl1 = AdvancedOrderTicketOTO::build_stop_limit(
            false, conv(end-3*incr), beg, sz
            );
    ids.insert( orderbook->insert_limit_order(true, beg, sz*10, ecb, aot_s1) );
    ids.insert( orderbook->insert_limit_order(false, end, sz*10, ecb, aot_sl1) );

    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);

    auto aot_m1 = AdvancedOrderTicketOTO::build_market(false, sz * 2);
    ids.insert( orderbook->insert_market_order(true, sz, ecb, aot_m1)); //1

    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);

    reinterpret_cast<ManagementInterface*>(orderbook)
            ->grow_book_above(end + incr);

    ids.insert( orderbook->insert_limit_order(false, conv(end+incr), sz*10, ecb) ); //2

    auto aot_sl2 = AdvancedOrderTicketOTO::build_stop_limit(
            true, conv(end+incr), conv(end+incr), sz
            );
    ids.insert( orderbook->insert_limit_order( true, conv(end+incr), 1000,
                                               ecb, aot_sl2) ); //3

    auto aot_l1 = AdvancedOrderTicketOTO::build_limit(true, conv(end+incr), sz);
    ids.insert( orderbook->insert_limit_order( true, conv(end+incr), sz,
                                               ecb, aot_l1 ) ); //4

    /*                                                                 v------------------------------|
     *                   end + 1 (2, sell 1000) (3.b buy 100) -> (3.d bstop 100)  (3.c buy 100) (4.a buy 100) (4.b buy 100) (4.c buy 100)
     *                                                    ||           |--------------|-----------------------------^
     *                                                    ||--------------|           |
     *                                                    |               |           |
     *                     end(sell 1000) (1.a, buy 100) (3.a, buy 900)   |           |
     *                            |            |                          |           |
     * end - 3 (1.b, sstop 100) <-| <----------|                          |--(1, buy stop)
     *            |                            |                                   A
     *     (1.e, sell 100)                     |                                   |
     *            |                            |                                   |
     * beg (buy 1000) <------------------------| (1.c, sell 200) ------------------|
     *
     *
     *
     *
     *                                           SL1000 SL900 SL800 SL700 SL600 SL500
     *                                end SL1000 SL900  0
     *
     *  beg BL1000 BL800 BL700
     *
     *  (1,100) (1,200) (1,100) (3, 900) (3,100) (1, 100) (4,100) (3,100) (4,100) = 1800
     */

    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    unsigned long long vol = orderbook->volume();

    if( bs != 700 ){
        return 1;
    }else if( as != 500 ){
        return 2;
    }else if( tbs != 700){
        return 3;
    }else if( tas != 500 ){
        return 4;
    }else if( vol != 1800 ){
        return 5;
    }

    out<< "pulls: " << boolalpha <<endl;
    for(auto id : ids ){
        if( orderbook->pull_order(id) ){
            out<< id << " ";
        }
    }
    out<< endl;

    auto md_book = orderbook->market_depth(nticks + 2);
    size_t book_sz = md_book.size();
    if( book_sz > 0 ){
        return 9;
    }else if( orderbook->total_bid_size() || orderbook->total_ask_size() ){
        return 10;
    }

    return 0;
}


int
TEST_advanced_OTO_3(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    orderbook->insert_limit_order(false, end, sz, ecb);
    orderbook->insert_limit_order(false, conv(end-incr), sz, ecb);
    orderbook->insert_limit_order(false, conv(end-2*incr), sz, ecb);

    auto aot = AdvancedOrderTicketOTO::build_market(true, sz);
    orderbook->insert_stop_order(true, conv(end-2*incr), sz, ecb, aot);
    dump_orders(orderbook,out);

    orderbook->insert_limit_order(true, conv(end-2*incr), sz, ecb);
    dump_orders(orderbook,out);

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */

