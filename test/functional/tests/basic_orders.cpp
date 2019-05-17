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

#include "../functional.hpp"

#ifdef RUN_FUNCTIONAL_TESTS

using namespace sob;
using namespace std;

namespace{
    size_t sz = 100;
}

int
TEST_basic_orders_1(sob::FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    int count = 0;

    for(double b = beg; b <= end; b=conv(b+incr) ){
        orderbook->insert_limit_order(true, b, sz);
        count++;
    }

    orderbook->insert_market_order(false, (count-1) * sz);

    size_t bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    size_t ts = orderbook->total_size();
    double bp = orderbook->bid_price();

    if( (bs != sz) || (tbs != sz) || (ts != sz) ){
        return 1;
    }else if( orderbook->volume() != (count-1)*sz ){
        return 2;
    }else if( orderbook->last_size() != sz ){
        return 3;
    }else if( bp != beg ){
        return 4;
    }else if( get<1>(orderbook->time_and_sales().back()) != conv(beg+incr) ){
        return 5;
    }

    return 0;
}

int
TEST_basic_orders_2(FullInterface *orderbook, std::ostream& out)
{
    static const AdvancedOrderTicket AOT_NULL = AdvancedOrderTicket::null;

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    double b = conv((beg + end) / 2);

    orderbook->insert_limit_order(true, b, sz, callback, AOT_NULL);
    orderbook->insert_limit_order(true, conv(b+incr), sz, callback, AOT_NULL);
    orderbook->insert_limit_order(true, conv(b+2*incr), sz, callback, AOT_NULL);

    orderbook->insert_stop_order(false, conv(b+2*incr), sz, callback, AOT_NULL);
    orderbook->insert_stop_order( false, conv(b+2*incr), conv(b+incr), sz,
                                  callback, AOT_NULL );

    orderbook->dump_limits(out);
    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);
    orderbook->dump_stops(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);

    orderbook->insert_market_order(false, static_cast<size_t>(sz/2), callback);
    // 100 <- s50.a s50.b
    // 100 <- s50.b s50.c
    // 100
    size_t bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    size_t as = orderbook->ask_size();

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);

    if( bs != sz || tbs != sz || as != static_cast<size_t>(sz*.5) ){
        return 1;
    }else if( orderbook->volume() != (2 * sz) ){
        return 2;
    }else if( orderbook->last_size() != static_cast<size_t>(.5 * sz) ){
        return 3;
    }else if( orderbook->bid_price() != b ){
        return 4;
    }else if( orderbook->ask_price() != b+incr ){
        return 5;
    }

    return 0;
}

int
TEST_stop_orders_1(FullInterface *orderbook, std::ostream& out)
{
    static const AdvancedOrderTicket AOT_NULL = AdvancedOrderTicket::null;

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    double mid = conv((beg + end) / 2);

    size_t bs, as, ts, vol, tot = 0, exp_vol=0;
    double ap;
    int c = 0;


    for(double i = mid -incr; i >= beg; i-= incr, ++c){
        orderbook->insert_stop_order(false, conv(i), sz);
        tot += sz;
    }

    orderbook->insert_limit_order(true, beg, tot+sz);
    orderbook->insert_market_order(false, sz);
    exp_vol = tot + sz;

    vol = orderbook->volume();
    bs = orderbook->bid_size();
    ts = orderbook->total_size();

    if( vol != exp_vol )
        return 2;

    if( ts != 0 )
        return 3;

    tot = 0;
    c = 0;
    for(double i = mid -incr; i >= beg; i-= incr, ++c){
        if( c % 2 == 0 ){
            for( int ii = 0; ii < 3; ++ii){
                orderbook->insert_stop_order(false, conv(i), sz);
                tot += sz;
            }
        }
    }

    orderbook->insert_limit_order(true, beg, tot+sz);
    orderbook->insert_market_order(false, sz);
    exp_vol += (tot + sz);

    vol = orderbook->volume();
    bs = orderbook->bid_size();
    ts = orderbook->total_size();
    if( vol != exp_vol )
        return 4;

    if( ts != 0 )
        return 5;

    tot = 0;
    c = 0;
    for(double i = mid -incr; i >= beg; i-= incr, ++c){
        if( c % 3 == 0 ){
            for( int ii = 0; ii < 3; ++ii){
                orderbook->insert_stop_order(false, conv(i), conv(beg), sz);
                tot += sz;
            }
        }else if( c %2 == 0){
            orderbook->insert_stop_order(false, conv(i), conv(beg), sz*2);
            tot += sz*2;
        }
    }

    orderbook->insert_limit_order(true, conv(mid-2*incr), sz);
    orderbook->insert_limit_order(true, conv(beg), tot);
    orderbook->insert_limit_order(false, conv(mid-4*incr), sz);
    exp_vol += (tot+sz);

    vol = orderbook->volume();
    bs = orderbook->bid_size();
    ts = orderbook->total_size();
    if( vol != exp_vol )
        return 6;

    if( ts != 0 )
        return 7;

    tot = 0;
    c = 0;
    for(double i = mid -incr; i >= beg; i-= incr, ++c){
        if( c % 3 == 0 ){
            for( int ii = 0; ii < 3; ++ii){
                orderbook->insert_stop_order(false, conv(i), conv(beg), sz);
                tot += sz;
            }
        }else if( c %2 == 0){
            orderbook->insert_stop_order(false, conv(i), conv(beg), sz*2);
            tot += sz*2;
        }
    }

    orderbook->insert_limit_order(true, conv(beg), sz);
    orderbook->insert_limit_order(false, conv(beg), sz);
    exp_vol += sz;

    vol = orderbook->volume();
    bs = orderbook->bid_size();
    ts = orderbook->total_size();
    as = orderbook->ask_size();
    ap = orderbook->ask_price();
    orderbook->dump_limits(out);
    orderbook->dump_stops(out);
    if( vol != exp_vol )
        return 8;

    if( bs != 0)
        return 9;

    if( ts != tot || as != tot)
        return 10;

    if( ap != beg )
        return 11;

    // beg                S limit tot

    orderbook->insert_stop_order(true, conv(beg), sz);
    orderbook->insert_stop_order(true, conv(beg+incr), sz);
    orderbook->insert_stop_order(true, conv(beg+incr), sz);
    orderbook->insert_stop_order(true, conv(beg+incr*4), sz);
    orderbook->insert_stop_order(true, conv(beg+incr*4), sz*2);
    orderbook->insert_stop_order(true, conv(beg+incr*4), sz*3);
    orderbook->insert_stop_order(true, conv(beg+incr*5), 1 );
    // sz*9 + 1

    // beg + 5  B stop 1
    // beg + 4  B stop 6*sz
    // beg + 3
    // beg + 2
    // beg + 1  B stop 2*sz
    // beg      B stop sz             S limit tot

    orderbook->insert_limit_order(false, conv(beg+incr*5), sz*10+2 );


    // beg + 5  B stop 1             S limit sz*10 + 2
    // beg + 4  B stop 6*sz
    // beg + 3
    // beg + 2
    // beg + 1  B stop 2*sz
    // beg      B stop sz             S limit tot

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);
    orderbook->insert_limit_order(true, conv(beg+incr*5), tot + sz);
    exp_vol += (tot + sz*10 + 1);

    vol = orderbook->volume();
    ts = orderbook->total_size();
    ap = orderbook->ask_price();
    if( vol != exp_vol || ts != 1 )
        return 12;

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);
    ap = orderbook->ask_price();
    if( ap != conv(beg+incr*5) )
        return 13;

    return 0;
}

int
TEST_basic_orders_ASYNC_1(FullInterface *orderbook, std::ostream& out)
{
    static const AdvancedOrderTicket AOT_NULL = AdvancedOrderTicket::null;

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    double b = conv((beg + end) / 2);

    std::future<id_type> F;

    F = orderbook->insert_limit_order_async(true, b, sz, callback, AOT_NULL);
    F.wait();

    F = orderbook->insert_limit_order_async(true, conv(b+incr), sz, callback, AOT_NULL);
    F.wait();

    F = orderbook->insert_limit_order_async(true, conv(b+2*incr), sz, callback, AOT_NULL);
    F.wait();

    F = orderbook->insert_stop_order_async(false, conv(b+2*incr), sz, callback, AOT_NULL);
    F.wait();

    F = orderbook->insert_stop_order_async( false, conv(b+2*incr), conv(b+incr), sz,
                                  callback, AOT_NULL );
    F.wait();

    orderbook->dump_limits(out);
    orderbook->dump_buy_limits(out);
    orderbook->dump_sell_limits(out);
    orderbook->dump_stops(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);

    F = orderbook->insert_market_order_async(false, static_cast<size_t>(sz/2), callback);
    F.wait();
    // 100 <- s50.a s50.b
    // 100 <- s50.b s50.c
    // 100
    size_t bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    size_t as = orderbook->ask_size();

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);

    if( bs != sz || tbs != sz || as != static_cast<size_t>(sz*.5) ){
        return 1;
    }else if( orderbook->volume() != (2 * sz) ){
        return 2;
    }else if( orderbook->last_size() != static_cast<size_t>(.5 * sz) ){
        return 3;
    }else if( orderbook->bid_price() != b ){
        return 4;
    }else if( orderbook->ask_price() != b+incr ){
        return 5;
    }

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */
