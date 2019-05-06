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

using namespace sob;
using namespace std;

namespace{
    map<id_type, id_type> ids;
    auto ecb = create_advanced_callback(ids);
    size_t sz = 100;
}

int
TEST_orders_info_pull_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    bool good_pull = false;
    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();
    int count = 0;

    ids.clear();

    vector<pair<id_type, double>> ids;
    for(double b = beg ; b < end; b = conv(b+incr) ){
        ids.emplace_back(orderbook->insert_limit_order(false, b, sz), conv(b));
        count++;
    }
    id_type stop_id = orderbook->insert_stop_order(true, beg, sz, callback);

    random_shuffle(ids.begin(), ids.end() - 1);
    out<< boolalpha;

    order_info of = orderbook->get_order_info(ids[0].first);
    if( of.limit != ids[0].second || of.size != sz ){
        return 1;
    }
    out<< "ORDER INFO: " << ids[0].first << " "<< of << endl;

    of = orderbook->get_order_info(ids[0].first);
    if( of.limit != ids[0].second || of.size != sz ){
        return 2;
    }
    out<< "ORDER INFO: " << ids[0].first << " " << of << endl;

    good_pull = orderbook->pull_order(ids[0].first);
    if( !good_pull ){
        return 3;
    }
    out<< "PULL: " << ids[0].first << " "<< good_pull << endl;
    --count;

    of = orderbook->get_order_info(stop_id);
    if( of.stop != beg || of.size != sz ){
        return 4;
    }
    out<< "ORDER INFO: " << stop_id << " "<< of << endl;

    of = orderbook->get_order_info(stop_id);
    if( of.stop != beg || of.size != sz ){
        return 5;
    }
    out<< "ORDER INFO: " << stop_id << " " << of << endl;

    good_pull = orderbook->pull_order(stop_id);
    if( !good_pull ){
        return 6;
    }
    out<< "PULL: " << stop_id << " "<< good_pull << endl;

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);

    stop_id = orderbook->insert_stop_order( false, conv(end-2*incr),
                                            conv(end-incr),  sz, callback);

    of = orderbook->get_order_info(stop_id);
    if( of.stop != conv(end-(2*incr))
        || of.limit != conv(end-incr)
        || of.size != sz ){
        return 7;
    }
    out<< "ORDER INFO: " << stop_id << " "<< of << endl;

    of = orderbook->get_order_info(stop_id);
    if( of.stop != conv(end-(2*incr))
        || of.limit != conv((end-incr))
        || of.size != sz ){
        return 8;
    }
    out<< "ORDER INFO: " << stop_id << " " << of << endl;

    good_pull = orderbook->pull_order(stop_id);
    if( !good_pull ){
        return 9;
    }
    out<< "PULL: " << stop_id << " "<< good_pull << endl;

    orderbook->dump_limits(out);
    orderbook->dump_stops(out);

    orderbook->insert_market_order(true, count * sz);

    size_t bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    size_t ts = orderbook->total_size();
    double bp = orderbook->bid_price();
    auto timesales = orderbook->time_and_sales();
    if( bs || tbs || ts  ){
        return 10;
    }else if( orderbook->volume() != (count*sz) ){
        return 11;
    }else if( orderbook->last_size() != sz ){
        return 12;
    }else if( bp != 0.0 ){
        return 13;
    }else if( get<1>(timesales.back()) != conv(end-incr) ){
        return 14;
    }

    return 0;
}


int
TEST_replace_order_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double incr = orderbook->tick_size();
    size_t tvol = 0;

    ids.clear();

    id_type id1 = orderbook->insert_limit_order(true, beg, sz, ecb);
    ids[id1] = id1;
    id_type id2 = orderbook->insert_limit_order(true, conv(beg+2*incr), sz, ecb);
    ids[id2] = id2;
    id_type id3 = orderbook->insert_limit_order(true, conv(beg+4*incr), sz, ecb);
    ids[id3] = id3;
    id_type id4 = orderbook->insert_stop_order( false, conv(beg+4*incr),
                                                conv(beg+incr), sz, ecb);
    ids[id4] = id4;
    // beg + 4  L 100     S 100
    // beg + 2  L 100
    // beg + 0  L 100
    dump_orders(orderbook, out);

    orderbook->pull_order(ids[id2]);
    // beg + 4  L 100     S 100
    //
    // beg + 0  L 100
    dump_orders(orderbook, out);

    orderbook->insert_market_order(false, static_cast<int>(sz/2), ecb);
    tvol += sz;
    // beg + 1            L 50
    // beg + 0  L 100
    dump_orders(orderbook, out);

    if( orderbook->bid_price() != conv(beg)
        && orderbook->ask_price() != conv(beg + incr) ){
        return 1;
    }

    orderbook->insert_market_order(true, static_cast<int>(sz/2) -1, ecb);
    tvol += (int(sz/2) -1);
    // beg + 1            L 1
    // beg + 0  L 100
    dump_orders(orderbook, out);

    if( orderbook->ask_size() != 1 ){
        return 2;
    }

    id_type id6 = orderbook->replace_with_limit_order( ids[id4], false,
                                                       conv(beg+2*incr), sz,
                                                       ecb );
    ids[id6] = id6;
    // beg + 2            L 100
    // beg + 1
    // beg + 0  L 100
    dump_orders(orderbook, out);

    if( orderbook->total_ask_size() != sz ){
        return 3;
    }else if( orderbook->ask_price() != conv(beg+incr*2) ){
        return 4;
    }

    // TODO fix the race w/ the callback (temporary fix)
    std::this_thread::sleep_for( std::chrono::milliseconds(100) );

    id_type id7 = orderbook->replace_with_stop_order( ids[id1], false,
                                                      conv(beg+4*incr), sz,
                                                      ecb );
    // beg + 4            S 100
    // beg + 2            L 100
    // beg + 0
    dump_orders(orderbook, out);

    // TODO fix the race w/ the callback (temporary fix)
    std::this_thread::sleep_for( std::chrono::milliseconds(100) );

    id_type id8 = orderbook->replace_with_stop_order( id7, false,
                                                      conv(beg+3*incr),
                                                      conv(beg+2*incr), 2*sz,
                                                      ecb );
    ids[id8] = id8;
    // beg + 3            S 200
    // beg + 2            L 100
    // beg + 0
    dump_orders(orderbook, out);

    auto aot = AdvancedOrderTicketOTO::build_limit(true, beg, sz);
    id_type id9 = orderbook->insert_market_order(true, sz, ecb, aot);
    tvol += sz;
    // ecb should have already handle order id change
    // beg + 3
    // beg + 2            L 200
    // beg + 0  L 100
    dump_orders(orderbook, out);

    if( orderbook->bid_price() != beg ){
        return 5;
    }else if( orderbook->bid_size() != sz ){
        return 6;
    }else if( orderbook->ask_price() != conv(beg + 2*incr) ){
        return 7;
    }else if( orderbook->ask_size() != (2*sz) ){
        return 8;
    }else if( orderbook->volume() != tvol ){
        return 9;
    }

    id_type id10 = orderbook->replace_with_limit_order( ids[id9], true,
                                                        conv(beg+2*incr), sz,
                                                        ecb, aot );
    tvol += sz;
    // beg + 3
    // beg + 2              L 100
    // beg + 0  L 100(new)
    dump_orders(orderbook, out);

    if( orderbook->bid_price() != beg ){
        return 10;
    }else if( orderbook->bid_size() != sz ){
        return 11;
    }else if( orderbook->ask_price() != conv(beg + 2*incr) ){
        return 12;
    }else if( orderbook->ask_size() != sz ){
        return 13;
    }else if( orderbook->volume() != tvol ){
        return 14;
    }

    auto aot2 = AdvancedOrderTicketOCO::build_stop_limit( true, conv(beg+2*incr),
                                                          conv(beg+3*incr), sz );
    id_type id11 = orderbook->replace_with_stop_order( ids[id10], false,
                                                       conv(beg+incr), sz,
                                                       ecb, aot2 );
    ids[id11] = id11;
    orderbook->insert_limit_order(true, beg, sz, ecb);
    // beg + 3
    // beg + 2  S 100(A)        L 100
    // beg + 1                  S 100(A)
    // beg + 0 L 100
    dump_orders(orderbook, out);

    orderbook->insert_market_order(true, static_cast<int>(sz/2));
    tvol += sz;
    // beg + 3 L 50
    // beg + 2
    // beg + 0 L 100
    dump_orders(orderbook, out);

    auto aot3 = AdvancedOrderTicketOCO::build_limit(false, beg, sz);
    orderbook->replace_with_limit_order( ids[id11], false, conv(beg+3*incr),
                                         sz, ecb, aot3 );
    tvol += sz;
    // beg + 4
    // beg + 3
    // beg + 2
    dump_orders(orderbook, out);

    if( orderbook->total_ask_size() != 0 ){
        return 15;
    }else if( orderbook->total_bid_size() != 0 ){
        return 16;
    }else if( orderbook->volume() != tvol ){
        return 17;
    }
    return 0;
}

#endif /* RUN_FUNCTIONAL_TESTS */


