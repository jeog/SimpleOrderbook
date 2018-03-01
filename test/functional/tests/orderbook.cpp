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

#include <map>
#include <vector>
#include <tuple>
#include <random>
#include <iostream>
#include <stdexcept>

#include "../../../include/tick_price.hpp"

using namespace sob;
using namespace std;

namespace {
    size_t sz = 100;
}

int
TEST_grow_1(FullInterface *full_orderbook)
{
    auto conv = [&](double d){ return full_orderbook->price_to_tick(d); };

    ManagementInterface *orderbook =
            dynamic_cast<ManagementInterface*>(full_orderbook);

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    size_t ticks = static_cast<size_t>((end - beg)/incr);

    vector<pair<int,bool>> grow_stats = {
            {1,true},
            {1,false},
            {static_cast<int>(ticks/2), false},
            {0,true},
            {0,false},
            {static_cast<int>(ticks/2), true},
            {ticks, true},
            {ticks, false}
    };

    map<double, pair<size_t,side_of_market>> md;

    size_t limit_filled = 0;
    bool cb_error = false;
    bool cb_active = true;
    order_exec_cb_type limit_cb =
        [&](callback_msg msg, id_type id1, id_type id2, double price, size_t s)
        {
            if( !cb_active ){
                return;
            }
            cout<< "limit_cb: active" << endl;
            limit_filled += s;
            try{
                auto val = md.at(price);
                if( val.first > s ){
                    md[price] = make_pair(val.first - sz, val.second);
                }else if( val.first == s ){
                    md.erase(price);
                }else{
                    throw runtime_error("size < s in test 4 callback");
                }
            }catch(...){
                cb_error = true;
            }
        };

    size_t total_bid_size = 0;
    size_t total_ask_size = 0;
    size_t total_buy_stop_size = 0;
    size_t total_sell_stop_size = 0;
    for(auto& p : grow_stats){
        double min = orderbook->min_price();
        double max = orderbook->max_price();
        double lower = conv(min + (max - min) / 4);
        double upper = conv(max - (max - min) / 4);
        double lower_mid = conv(min + (max - min) / 2.5);
        double upper_mid = conv(max - (max - min) / 2.5);

        orderbook->insert_limit_order(true, min, sz, limit_cb);
        md[min] = make_pair(md[min].first + sz, side_of_market::bid);
        total_bid_size += sz;

        orderbook->insert_limit_order(true, lower, sz, limit_cb);
        md[lower] = make_pair(md[lower].first + sz, side_of_market::bid);
        total_bid_size += sz;

        orderbook->insert_limit_order(true, lower_mid, sz, limit_cb);
        md[lower_mid] = make_pair(md[lower_mid].first + sz, side_of_market::bid);
        total_bid_size += sz;

        orderbook->insert_stop_order(false, lower, sz);
        total_sell_stop_size += sz;

        orderbook->insert_limit_order(false, max, sz, limit_cb);
        md[max] = make_pair(md[max].first + sz, side_of_market::ask);
        total_ask_size += sz;

        orderbook->insert_limit_order(false, upper, sz, limit_cb);
        md[upper] = make_pair(md[upper].first + sz, side_of_market::ask);
        total_ask_size += sz;

        orderbook->insert_limit_order(false, upper_mid, sz, limit_cb);
        md[upper_mid] = make_pair(md[upper_mid].first + sz, side_of_market::ask);
        total_ask_size += sz;

        orderbook->insert_stop_order(true, upper, sz);
        total_buy_stop_size += sz;

        if(cb_error){
            return 1;
        }

        if( p.second ){
            orderbook->grow_book_above(max + (p.second * incr));
        }else{
            orderbook->grow_book_below(min - (p.second * incr));
        }

        auto md_book = orderbook->market_depth(ticks + 2);
        if( md != md_book ){
            cout << "*** ERROR (2) ***" << endl;
            for( auto& pp : md ){
                cout<< pp.first << " " << pp.second.first
                                     << " " << pp.second.second << '\t';
                try{
                    auto v = md_book.at(pp.first);
                    cout<< v.first << " " << v.second;
                }catch(...){
                }
                cout<< endl;
            }
            return 2;
        }
    }
    cb_active = false;

    if( orderbook->total_bid_size() != total_bid_size ){
        return 3;
    }else if( orderbook->total_ask_size() != total_ask_size ){
        return 4;
    }else{
        try{
            auto ssz = total_bid_size - limit_filled - total_sell_stop_size;
            orderbook->insert_market_order(false, static_cast<size_t>(ssz));

            auto bsz = total_ask_size - limit_filled - total_buy_stop_size;
            orderbook->insert_market_order(true, static_cast<size_t>(bsz));
        }catch(liquidity_exception&){
            return 5;
        }
    }

    unsigned long long vol = orderbook->volume();
    if( vol != total_bid_size + total_ask_size ){
        return 6;
    }else if( orderbook->bid_size() != 0 ){
        return 7;
    }else if( orderbook->ask_size() != 0 ){
        return 8;
    }

    return 0;
}


// TODO expand these
int
TEST_tick_price_1()
{
    typedef TickPrice<quarter_tick> tp_t;

    vector<tp_t> tps = {
        tp_t(-1000001),
        tp_t(-10),
        tp_t(-5),
        tp_t(-4),
        tp_t(-3),
        tp_t(0),
        tp_t(3),
        tp_t(4),
        tp_t(5),
        tp_t(10),
        tp_t(999999999)
    };

    long wholes[] = {-250001, -3,-2,-1,-1,0,0,1,1,2, 249999999};

    long ticks[] = {-1000001, -10, -5, -4, -3, 0, 3, 4, 5, 10, 999999999};

    double doubles[] = {-250000.25, -2.5, -1.25, -1, -.75, 0,
                        .75, 1, 1.25, 2.5, 249999999.75 };

    for( size_t i = 0; i < tps.size(); ++ i ){
        long w = tps[i].as_whole();
        long t = tps[i].as_ticks();
        double d = static_cast<double>(tps[i]);
        if( w != wholes[i]){
            return 1;
        }else if( t != ticks[i] ){
            return 2;
        }else if( d != doubles[i] ){
            return 3;
        }
    }

    vector<tp_t> tps2 = {
        tp_t(-250000.20),
        tp_t(-2.5),
        tp_t(-1.13),
        tp_t(-1.12499),
        tp_t(-.7),
        tp_t(-.125),
        tp_t(.75),
        tp_t(1.1),
        tp_t(1.37),
        tp_t(2.375),
        tp_t(249999999.874999)
    };

    for( size_t i = 0; i < tps2.size(); ++ i ){
        long w = tps2[i].as_whole();
        long t = tps2[i].as_ticks();
        double d = static_cast<double>(tps2[i]);
        if( w != wholes[i]){
            return 4;
        }else if( t != ticks[i] ){
            return 5;
        }else if( d != doubles[i] ){
            return 6;
        }
    }
    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */

