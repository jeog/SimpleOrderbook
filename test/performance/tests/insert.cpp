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

#include "../performance.hpp"

#ifdef RUN_PERFORMANCE_TESTS

#include <chrono>
#include <stdexcept>

using namespace std;
using namespace sob;

double
TEST_n_limits(FullInterface *ob, int n)
{
    auto prices = generate_prices(ob, ob->min_price(), ob->max_price(), n);
    auto sizes = generate_sizes(1, 1000000, n);
    auto buy_sells = generate_buy_sells(n);
    id_type id = 0;

    auto start = chrono::steady_clock::now();
    for(int i = 0; i < n; ++i){
        id = ob->insert_limit_order( buy_sells[i], prices[i], sizes[i] );
        if( !id ){
            throw runtime_error("insert limit order failed");
        }
    }
    auto end = chrono::steady_clock::now();
    chrono::duration<double> sec = end - start;
    return sec.count();
}


double
TEST_n_basics(FullInterface *ob, int n)
{
    auto prices = generate_prices(ob, ob->min_price(), ob->max_price(), n);
    auto sizes = generate_sizes(1, 1000000, n);
    auto buy_sells = generate_buy_sells(n);
    auto order_types = generate_limit_market_stop(n, 2);
    id_type id = 0;

    /* avoid liquidity exc */
    ob->insert_limit_order( true, ob->min_price(), n * 1000);
    ob->insert_limit_order( false, ob->max_price(), n * 1000);

    auto start = chrono::steady_clock::now();
    for(int i = 0; i < n; ++i){
        switch( order_types[i] ){
        case order_type::market:
            id = ob->insert_market_order( buy_sells[i], sizes[i] );
            break;
        case order_type::limit:
            id = ob->insert_limit_order( buy_sells[i], prices[i], sizes[i] );
            break;
        case order_type::stop:
            id = ob->insert_stop_order( buy_sells[i], prices[i], sizes[i] );
            break;
        case order_type::stop_limit:
            id = ob->insert_stop_order( buy_sells[i], prices[i], prices[i], sizes[i] );
            break;
        default:
            throw runtime_error("invalid order type");
        }
        if( !id ){
            throw runtime_error("insert " + order_types[i] + " failed");
        }
    }

    auto end = chrono::steady_clock::now();
    chrono::duration<double> sec = end - start;
    return sec.count();
}

#endif /* RUN_PERFORMANCE_TESTS */
