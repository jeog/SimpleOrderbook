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
#include <algorithm>

using namespace sob;

double
TEST_n_pulls(FullInterface *ob, int n)
{
    double mid = ob->price_to_tick((ob->max_price() + ob->min_price()) / 2);
    auto prices = generate_prices(ob, ob->min_price(), ob->max_price(), n);
    auto sizes = generate_sizes(1, 1000000, n);
    auto buy_sells = generate_buy_sells(n);
    auto order_types = generate_limit_stop(n, 2);
    id_type id = 0;
    std::vector<id_type> active_ids;

    /* no trades should occur */
    for(int i = 0; i < n; ++i){
        switch( order_types[i] ){
        case order_type::limit:
            id = ob->insert_limit_order( prices[i] < mid, prices[i], sizes[i] );
            break;
        case order_type::stop:
            id = ob->insert_stop_order( buy_sells[i], prices[i], sizes[i] );
            break;
        case order_type::stop_limit:
            id = ob->insert_stop_order( buy_sells[i], prices[i], prices[i], sizes[i]);
            break;
        default:
            throw std::runtime_error("invalid order type");
        }
        if( !id ){
            throw std::runtime_error("insert limit failed");
        }
        active_ids.push_back(id);
    }

    std::random_shuffle(active_ids.begin(), active_ids.end());

    auto start = std::chrono::steady_clock::now();
    for( id_type i : active_ids ){
        if( !ob->pull_order(i) ){
            throw std::runtime_error("failed to pull");
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> sec = end - start;
    return sec.count();
}


double
TEST_n_replaces(FullInterface *ob, int n)
{
    double mid = ob->price_to_tick((ob->max_price() + ob->min_price()) / 2);
    auto prices = generate_prices(ob, ob->min_price(), ob->max_price(), n);
    auto sizes = generate_sizes(1, 1000000, n);
    auto buy_sells = generate_buy_sells(n);
    auto order_types = generate_limit_stop(n, 2);
    id_type id = 0;
    std::vector<id_type> active_ids;

    /* no trades should occur */
    for(int i = 0; i < n; ++i){
        switch( order_types[i] ){
        case order_type::limit:
            id = ob->insert_limit_order( prices[i] < mid, prices[i], sizes[i] );
            break;
        case order_type::stop:
            id = ob->insert_stop_order( buy_sells[i], prices[i], sizes[i] );
            break;
        case order_type::stop_limit:
            id = ob->insert_stop_order( buy_sells[i], prices[i], prices[i], sizes[i]);
            break;
        default:
            throw std::runtime_error("invalid order type");
        }
        if( !id ){
            throw std::runtime_error("insert limit failed");
        }
        active_ids.push_back(id);
    }

    std::random_shuffle(active_ids.begin(), active_ids.end());
    std::random_shuffle(prices.begin(), prices.end());
    std::random_shuffle(sizes.begin(), sizes.end());
    std::random_shuffle(buy_sells.begin(), buy_sells.end());
    std::random_shuffle(order_types.begin(), order_types.end());

    auto start = std::chrono::steady_clock::now();
    for(int i = 0; i <n; ++i){
        switch( order_types[i] ){
        id = active_ids[i];
        case order_type::limit:
            id = ob->replace_with_limit_order( id, prices[i] < mid, prices[i],
                                               sizes[i] );
            break;
        case order_type::stop:
            id = ob->replace_with_stop_order( id, buy_sells[i], prices[i],
                                              sizes[i] );
            break;
        case order_type::stop_limit:
            id = ob->replace_with_stop_order( id, buy_sells[i], prices[i],
                                              prices[i], sizes[i] );
            break;
        default:
            throw std::runtime_error("invalid order type");
        }
        if( !id ){
            throw std::runtime_error("insert limit failed");
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> sec = end - start;
    return sec.count();
}

#endif /* RUN_PERFORMANCE_TESTS */

