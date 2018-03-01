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
}

int
TEST_advanced_BRACKET_1(FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            conv(end-10*incr), conv(end-12*incr), conv(end-6*incr), sz
            );

    cout<< "AdvancedOrderTicketBRACKET: " << aot << endl;
    cout<< "OrderParamaters 1: " << *aot.order1() << endl;
    cout<< "OrderParamaters 2: " << *aot.order2() << endl;

    orderbook->insert_limit_order(true, conv(end-8*incr), sz, ecb, aot);
    orderbook->insert_market_order(false, sz);
    // end - 6      L 100
    // end - 8
    // end - 10     S 100
    dump_orders(orderbook);

    if( orderbook->volume() != sz ){
        return 1;
    }else if( orderbook->ask_price() != conv(end-6*incr) ){
        return 2;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook);

    if( orderbook->volume() != 2*sz ){
        return 3;
    }else if( orderbook->total_ask_size() != 0 ){
        return 4;
    }

    return 0;
}


int
TEST_advanced_BRACKET_2(FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(false, beg, 2*sz);

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            conv(end-10*incr), conv(end-12*incr), conv(end-6*incr), sz
            );

    orderbook->insert_limit_order(true, conv(end-8*incr), sz, ecb, aot);
    // end - 6      L 100
    // end - 8
    // end - 10     S 100
    // ..
    // beg          L 100
    dump_orders(orderbook);

    if( orderbook->volume() != sz ){
        return 1;
    }else if( orderbook->total_ask_size() != 2*sz ){
        return 2;
    }

    orderbook->insert_market_order(true, sz);
    //
    // end - 12     L 100
    //
    dump_orders(orderbook);

    orderbook->insert_limit_order(true, end, 2*sz, ecb);
    dump_orders(orderbook);

    if( orderbook->volume() != 3*sz ){
        return 3;
    }else if( orderbook->ask_price() != 0 ){
        return 4;
    }else if( orderbook->bid_price() != end ){
        return 5;
    }else if( get<1>(orderbook->time_and_sales().back()) != conv(end-12*incr) ){
        return 6;
    }

    return 0;
}


int
TEST_advanced_BRACKET_3(FullInterface *orderbook)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketOTO::build_limit(false, beg, sz);
    orderbook->insert_limit_order(true, mid, sz, ecb, aot);
    //
    // mid  L 100
    //

    auto aot2 = AdvancedOrderTicketBRACKET::build_buy_stop(
            conv(mid+10*incr), conv(mid-10*incr), static_cast<size_t>(sz/2)
            );
    orderbook->insert_limit_order(false, mid, sz, ecb, aot2);
    //
    // beg         L 50
    //
    dump_orders(orderbook);

    // NOTE the initial OTO limit is inserted BEFORE the BRACKET limit
    if( orderbook->volume() != static_cast<unsigned long>(1.5 * sz) ){
        return 1;
    }else if( get<1>(orderbook->time_and_sales().back()) != beg ){
        return 2;
    }else if( orderbook->total_ask_size() != static_cast<size_t>(.5 * sz) ){
        return 3;
    }

    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */

