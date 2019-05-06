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

// TODO query AON sizes


// insert standard, then AONs
int
TEST_advanced_AON_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    size_t bs, tbs, as, tas, tv;

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    id_type id = 0, id2 = 0;

    id = orderbook->insert_limit_order(false, conv(mid + incr), sz, ecb);
    id = orderbook->insert_limit_order(true, conv(mid+incr), sz, ecb);
    dump_orders(orderbook,out);

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    if( bs != 0 || tbs != 0 )
        return 1;

    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    if( as != 0 || tas != 0 )
        return 2;

    tv = orderbook->volume();
    if( tv != sz )
        return 3;

    id = orderbook->insert_limit_order(false, conv(mid + incr), sz, ecb);
    id = orderbook->insert_limit_order(false, conv(mid + incr*2), sz, ecb);
    id = orderbook->insert_limit_order(true, conv(mid+incr*2), sz * 2, ecb, aot);
    dump_orders(orderbook,out);

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    if( bs != 0 || tbs != 0 )
        return 4;

    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    if( as != 0 || tas != 0 )
        return 5;

    tv = orderbook->volume();
    if( tv != sz * 3 )
        return 6;

    id = orderbook->insert_limit_order(false, conv(mid + incr), sz, ecb);
    id = orderbook->insert_limit_order(false, conv(mid + incr*2), sz, ecb);
    id2 = orderbook->insert_limit_order(true, conv(mid+incr*2), sz * 1.5, ecb, aot);
    dump_orders(orderbook,out);

    // doesn't include AON (yet)
    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    if( bs != 0 || tbs != 0 )
        return 7;

    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    if( as != sz*.5 || tas != sz*.5 )
        return 8;

    tv = orderbook->volume();
    if( tv != sz * 4.5 )
        return 9;

    dump_orders(orderbook,out);

    auto oi = orderbook->get_order_info(id);
    out<< "ORDER INFO: " << id << " " << oi << endl;
    if( oi.limit != conv(mid + incr*2) )
        return 10;
    if( oi.size != sz*.5 )
        return 11;

    if( !orderbook->pull_order(id) )
        return 12;

    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(id2);
    if( oi )
        return 13;

    if( orderbook->pull_order(id2) )
        return 14;

    if( orderbook->market_depth().size() )
        return 15;

    dump_orders(orderbook,out);


    return 0;
}



// insert AONs then standards
int
TEST_advanced_AON_2(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    size_t bs, tbs, as, tas, tv, ts;

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    id_type id = 0, id2 = 0;

    id = orderbook->insert_limit_order(false, conv(mid + incr), sz, ecb, aot);
    id2 = orderbook->insert_limit_order(false, conv(mid+incr*2), sz, ecb, aot);

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    tv = orderbook->volume();
    if( bs != 0 || tbs != 0 || as != 0 || tas != 0 || tv != 0 )
        return 1;

    tbs = orderbook->total_aon_bid_size();
    if( tbs != 0 )
        return 2;

    tas = orderbook->total_aon_ask_size();
    if( tas != 2*sz )
        return 3;

    ts = orderbook->total_aon_size();
    if( tas != 2*sz )
        return 4;

    auto md = orderbook->aon_market_depth();
    if( md.size() != 2 || md.find(mid+incr) == md.end()
        || md.find(mid+incr*2) == md.end() )
        return 5;

    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);

    auto oi = orderbook->get_order_info(id);
    out<< "ORDER INFO: " << id << " " << oi << endl;
    oi = orderbook->get_order_info(id2);
    out<< "ORDER INFO: " << id2 << " " << oi << endl;
    if( oi.limit != conv(mid + incr*2) )
        return 6;
    if( oi.advanced != aot )
        return 7;

    id = orderbook->insert_limit_order(true, conv(mid+incr*2), sz * 2.5, ecb);

    bs = orderbook->bid_size();
    tbs = orderbook->bid_size();
    ts = orderbook->total_size();
    if( bs != (sz/2) || tbs != (sz/2) || ts != (sz/2) )
        return 8;

    tbs = orderbook->total_aon_bid_size();
    ts = orderbook->total_aon_size();
    if( tbs != 0 || ts != 0 )
        return 9;

    md = orderbook->aon_market_depth();
    if( !md.empty() )
        return 10;

    dump_orders(orderbook, out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);
    return 0;
}

int
TEST_advanced_AON_3(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    size_t bs, tbs, as, tas, tv, ts;

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    id_type id = 0, id2 = 0;

    id = orderbook->insert_limit_order(false, conv(mid + incr), sz, ecb, aot);
    id2 = orderbook->insert_limit_order(false, conv(mid+incr*2), sz, ecb, aot);

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    tv = orderbook->volume();
    if( bs != 0 || tbs != 0 || as != 0 || tas != 0 || tv != 0 )
        return 1;

    tbs = orderbook->total_aon_bid_size();
    if( tbs != 0 )
        return 2;

    tas = orderbook->total_aon_ask_size();
    if( tas != 2*sz )
        return 3;

    ts = orderbook->total_aon_size();
    if( tas != 2*sz )
        return 4;

    auto amd = orderbook->aon_market_depth();
    if( amd.size() != 2 || amd.find(mid+incr) == amd.end()
        || amd.find(mid+incr*2) == amd.end() )
        return 5;

    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);

    id = orderbook->insert_limit_order(true, conv(mid+incr*2), sz * 1.5, ecb);
    // mid + 2    50       100 AON

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    if( bs != sz/2 || tbs != sz/2 || as != 0 || tas != 0 )
        return 6;

    double ap = orderbook->ask_price();
    double bp = orderbook->bid_price();
    if( bp != mid+incr*2 || ap != 0 )
        return 7;

    auto md = orderbook->market_depth();
    if( md.size() != 1 )
        return 8;

    auto p = md[mid+incr*2];
    if( p.first != sz/2 || p.second != side_of_market::bid )
        return 9;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    ts = orderbook->total_aon_size();
    if( tbs != 0 || tas != sz || ts != sz )
        return 10;

    amd = orderbook->aon_market_depth();
    if( amd.size() != 1 )
        return 11;

    auto pp = amd[mid+incr*2];
    if( pp.first != 0 || pp.second != sz )
        return 12;

    id2 = orderbook->insert_limit_order(true, conv(mid+incr*2), sz/2, ecb);

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    tas = orderbook->total_ask_size();
    if( bs != 0 || tbs != 0 || as != 0 || tas != 0 )
        return 13;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    ts = orderbook->total_aon_size();
    if( tbs != 0 || tas != 0 || ts != 0 )
        return 14;

    tv = orderbook->volume();
    if( tv != 2*sz )
        return 15;

    md = orderbook->market_depth();
    if( !md.empty() )
        return 16;

    amd = orderbook->aon_market_depth();
    if( !amd.empty() )
        return 17;

    if( orderbook->pull_order(id) )
        return 18;

    if( orderbook->pull_order(id2) )
        return 19;

    return 0;
}

int
TEST_advanced_AON_4(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    size_t bs, tbs, as, tas, v, ts;

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    id_type id = 0, id2 = 0, id3 = 0, id4 = 0;

    id = orderbook->insert_limit_order(true, conv(mid - incr), sz, ecb, aot);
    id2 = orderbook->insert_limit_order(false, conv(mid+incr), sz, ecb, aot);

    if( !orderbook->pull_order(id) )
        return 1;

    if( !orderbook->pull_order(id2) )
        return 2;

    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    v = orderbook->volume();
    if( tbs != 0 || tas != 0 || v != 0 )
        return 3;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 0 || tas != 0 )
        return 4;

    id = orderbook->insert_limit_order(true, conv(beg), sz, ecb, aot);
    id2 = orderbook->insert_limit_order(false, conv(end), sz, ecb, aot);

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != sz || tas != sz )
         return 5;

    id3 = orderbook->insert_limit_order(false, conv(beg), sz-1, ecb);

    tas = orderbook->total_ask_size();
    tbs = orderbook->total_aon_bid_size();
    if( tas != sz-1 || tbs != sz )
        return 6;

    v = orderbook->volume();
    if( v != 0)
        return 7;

    if( !orderbook->pull_order(id3) )
        return 8;


    id4 = orderbook->insert_limit_order(true, conv(end), sz/2, ecb);

    bs = orderbook->bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tas != sz || bs != sz/2 )
        return 9;

    v = orderbook->volume();
    if( v != 0)
        return 10;

    if( !orderbook->pull_order(id) )
        return 11;

    if( !orderbook->pull_order(id2) )
        return 12;

    ts = orderbook->total_aon_size();
    auto amd = orderbook->aon_market_depth();
    if( ts != 0 or amd.size() > 0 )
        return 13;

    ts = orderbook->total_bid_size();
    auto md = orderbook->market_depth();
    if( ts != sz/2 || md.size() != 1 || md[end].first != sz/2 )
        return 14;

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    id = orderbook->insert_limit_order(true, conv(end), sz*1.5, ecb, aot);

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    id2 = orderbook->insert_limit_order(false, conv(end), sz*3, ecb, aot);

    tas = orderbook->total_aon_ask_size();
    tbs = orderbook->total_aon_bid_size();
    if( tas != sz*3 || tbs != sz*1.5 )
        return 15;

    id3 = orderbook->insert_limit_order(true, conv(end), sz, ecb, aot);

    v = orderbook->volume();
    if( v != sz *3 )
        return 16;

    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    ts = orderbook->total_aon_size();
    if( tbs != 0 || tas != 0 || ts != 0 )
        return 17;

    if( orderbook->pull_order(id) )
        return 18;

    if( orderbook->pull_order(id2) )
        return 19;

    if( orderbook->pull_order(id3) )
        return 20;

    if( orderbook->pull_order(id4) )
        return 21;

    return 0;
}


int
TEST_advanced_AON_5(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    size_t bs, tbs, as, tas, v, ts;

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();


    // limit aon limit
    orderbook->insert_limit_order(true, conv(beg), sz, ecb);
    orderbook->insert_limit_order(true, conv(beg), sz, ecb, aot);
    id_type id3 = orderbook->insert_limit_order(true, conv(beg), sz, ecb);

    // beg 100<1> 100aon<2> 100<3>

    orderbook->insert_market_order(false, sz*1.5, ecb);

    // beg 100aon<2> 50<3>

    v = orderbook->volume();
    bs = orderbook->bid_size();
    tbs = orderbook->total_aon_bid_size();
    if( v != sz*1.5 || bs != sz/2 || tbs != sz )
        return 1;

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    if( !orderbook->pull_order(id3) )
        return 2;

    // beg 100 aon<2>

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    bs = orderbook->bid_size();
    tbs = orderbook->total_aon_bid_size();
    if(  bs != 0 || tbs != sz )
        return 3;

    id_type id5 = orderbook->insert_limit_order(true, conv(beg+incr), sz, ecb);
    id_type id6 = orderbook->insert_limit_order(true, conv(beg+incr), sz, ecb);
    id_type id7 = orderbook->insert_limit_order(true, conv(beg+incr), sz, ecb, aot);

    // beg + 1    100 <5>     100 <6>    100 aon <7>
    // beg        100 aon<2>

    orderbook->insert_stop_order(false, conv(beg+incr), sz/4, ecb); // 8

    // beg + 1    100 <5>     100 <6>    100 aon <7>          25S <8>
    // beg        100 aon<2>

    orderbook->insert_market_order(false, sz/4, ecb); // 9

    // beg + 1    50 <5>     100 <6>    100 aon <7>
    // beg        100 aon<2>

    // id 10 for stop -> limit

    v = orderbook->volume();
    tbs = orderbook->total_aon_bid_size();
    if( v != 2*sz || tbs != 2*sz )
        return 4;

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    if( bs != 1.5 * sz || tbs != 1.5 * sz)
        return 5;

    orderbook->insert_limit_order(true, conv(beg+incr), sz, ecb, aot); // 11

    // beg + 1    50 <5>     100 <6>    100 aon <7>   100  aon <11>
    // beg        100 aon<2>

    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    if( !orderbook->pull_order(id6) )
        return 7;

    if( !orderbook->pull_order(id5) )
        return 8;

    if( !orderbook->pull_order(id7) )
        return 9;


    // beg + 1    100 aon <11>
    // beg        100 aon<2>

    tbs = orderbook->total_aon_bid_size();
    ts = orderbook->total_size();
    if( tbs != 2*sz || ts != 0 )
        return 10;

    orderbook->insert_limit_order(true, conv(beg+incr*2), sz/2, ecb);
    id_type id13 = orderbook->insert_limit_order(true, conv(beg+incr), sz/2, ecb);
    orderbook->insert_limit_order(true, conv(beg+incr), sz/2, ecb, aot);
    orderbook->insert_limit_order(true, conv(beg), sz/2, ecb); // 15

    // beg + 2    50 <12>
    // beg + 1    100 aon <11>   50 <13>  50 aon<14>
    // beg        100 aon<2>  50 <15>

    tbs = orderbook->total_aon_bid_size();
    if( tbs != 2.5 * sz )
        return 11;

    bs = orderbook->bid_size();
    tbs = orderbook->total_bid_size();
    if( bs != .5 * sz || tbs != 1.5 * sz )
        return 12;

    if( !orderbook->pull_order(id13) )
        return 13;

    // beg + 2    50 <12>
    // beg + 1    100 aon <11>   50 aon<14>
    // beg        100 aon<2>  50 <15>

    tbs = orderbook->total_aon_bid_size();
    if( tbs != 2.5 *sz )
        return 14;

    orderbook->insert_market_order(false, sz/2, ecb); // 16

    // beg + 2
    // beg + 1    100 aon <11>   50 aon<14>
    // beg        100 aon<2>  50 <15>

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);

    double bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    if( bp != beg  || bs != sz/2 )
        return 15;

    orderbook->insert_limit_order(false, conv(beg+incr), sz/2, ecb); // 17


    // beg + 1    100 aon <11>   50 aon<14>   <<<should match>>>  50 <17>
    // beg        100 aon<2>  50 <15>

    // ...

    // beg + 1    100 aon <11>
    // beg        100 aon<2>  50 <15>

    bs = orderbook->bid_size();
    tbs = orderbook->total_aon_bid_size();
    if( bs != sz/2 || tbs != 2*sz )
        return 16;

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);

    orderbook->insert_limit_order(false, conv(beg+incr), sz/2, ecb, aot); // 18

    // beg + 1    100 aon <11>                50 aon <18>
    // beg        100 aon<2>  50 <15>

    orderbook->insert_limit_order(false, conv(beg+incr), sz/4, ecb, aot); // 19

    // beg + 1    100 aon <11>                50 aon <18> 25 aon <19>
    // beg        100 aon<2>  50 <15>


    //
    // TODO - what about when limit is @ beg and size > sz/4
    //
    orderbook->insert_limit_order(false, conv(beg+incr), sz/4, ecb, aot); // 20

    // beg + 1    100 aon <11>          << should match >> 50 aon <18> 25 aon <19> 25 <20>
    // beg        100 aon<2>  50 <15>

    // ...

    // beg + 1
    // beg        100 aon<2>  50 <15>

    dump_orders(orderbook,out);
    orderbook->dump_aon_sell_limits(out);
    orderbook->dump_aon_buy_limits(out);

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    if( bp != beg || bs != sz/2 )
        return 17;

    tbs = orderbook->total_aon_bid_size();
    if( tbs != sz )
        return 18;


    return 0;
}

int
TEST_advanced_AON_6(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr), sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz/2, ecb );
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/4, ecb);

    // mid + 1    100 aon <1>          50 <3> 25 <4>
    // mid        50 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid || ap != mid+incr )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_aon_bid_size();
    size_t tas = orderbook->total_aon_ask_size();
    if( bs != sz/2 || as != (3*sz/4) || tbs != sz || tas != 0 )
        return 2;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid), sz/2, ecb );

    // mid + 1    100 aon <1>  --------  50 <3> 25 <4>
    // mid        50 <2>           \___  S50 <5> (should match)

    // ..

    // mid + 1
    // mid        25 <2>

    unsigned long long v = orderbook->volume();
    if( v!= (sz*5/4) )
        return 3;

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != sz / 4 || as != 0 || ts != 0 )
        return 4;

    return 0;

}

int
TEST_advanced_AON_7(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/5, ecb );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/2, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/2, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/4, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/4, ecb, aot);

    // mid + 4    200 aon <1>          50 <4> 50 <5>
    // mid + 3                         25 <6>
    // mid + 2    20 <3>               25 aon <7>
    // mid + 1                         25 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid+2*incr || ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz/5 || as != sz/4 || tbs != sz+(sz/5) || tas != (5*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz/2 )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid), sz/5, ecb, aot );

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>        50 <4> 50 <5>
    // mid + 3                        25 <6>
    // mid + 2    20 <3>  ---         25 aon <7>
    // mid + 1               \        25 aon <8>
    // mid        100 <2>     \_____  S20 <9> aon (should only match)

    // ..


    // mid + 4    200 aon <1>        50 <4> 50 <5>
    // mid + 3                       25 <6>
    // mid + 2                       25 aon <7>
    // mid + 1                       25 aon <8>
    // mid        100 <2>

    unsigned long long v = orderbook->volume();
    if( v!= sz/5 )
        return 4;

    bp = orderbook->bid_price();
    ap = orderbook->ask_price();
    if( bp != mid|| ap != mid+incr*3 )
        return 5;

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    if( bs != sz || as != sz/4 || tbs != sz || tas != (5*sz/4) )
        return 6;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz/2 )
        return 7;

    return 0;

}


int
TEST_advanced_AON_8(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/4, ecb );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/2, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb, aot);

    // mid + 4    200 aon <1>          25 <4> 25 <5>
    // mid + 3                         25 <6>
    // mid + 2    25 <3>               50 aon <7>
    // mid + 1                         50 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid+2*incr || ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz/4 || as != sz/4 || tbs != (5*sz)/4 || tas != (3*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid), sz, ecb);

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>  -------  25 <3> 25 <4>
    // mid + 3                    \___  25 <5>
    // mid + 2    25 <3>  ---     \___  50 aon <6>
    // mid + 1               \    \___  50 aon <7>
    // mid        100 <2>  --\____\___  S100 <8> (should match)

    // ..

    // mid + 1
    // mid        50 <2>

    unsigned long long v = orderbook->volume();
    if( v!= sz*2.75 )
        return 4;

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != sz / 2 || as != 0 || ts != 0 )
        return 5;

    auto tsales = orderbook->time_and_sales();
    for(auto& t : tsales)
        out<< t << std::endl;

    return 0;

}

int
TEST_advanced_AON_9(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/5, ecb );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/2, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/2, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/4, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/4, ecb, aot);

    // mid + 4    200 aon <1>          50 <4> 50 <5>
    // mid + 3                         25 <6>
    // mid + 2    20 <3>               25 aon <7>
    // mid + 1                         25 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid+2*incr || ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz/5 || as != sz/4 || tbs != sz+(sz/5) || tas != (5*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz/2 )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_market_order(false, sz/5, ecb );

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>        50 <4> 50 <5>
    // mid + 3                        25 <6>
    // mid + 2    20 <3>  ---         25 aon <7>
    // mid + 1               \        25 aon <8>
    // mid        100 <2>     \_____  S20 <9> (should only match)

    // ..


    // mid + 4    200 aon <1>        50 <4> 50 <5>
    // mid + 3                       25 <6>
    // mid + 2                       25 aon <7>
    // mid + 1                       25 aon <8>
    // mid        100 <2>

    unsigned long long v = orderbook->volume();
    if( v!= sz/5 )
        return 4;

    bp = orderbook->bid_price();
    ap = orderbook->ask_price();
    if( bp != mid|| ap != mid+incr*3 )
        return 5;

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    if( bs != sz || as != sz/4 || tbs != sz || tas != (5*sz/4) )
        return 6;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz/2 )
        return 7;

    return 0;

}


int
TEST_advanced_AON_10(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/4, ecb );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/2, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb, aot);

    // mid + 4    200 aon <1>          25 <4> 25 <5>
    // mid + 3                         25 <6>
    // mid + 2    25 <3>               50 aon <7>
    // mid + 1                         50 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid+2*incr || ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz/4 || as != sz/4 || tbs != (5*sz)/4 || tas != (3*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != 2* sz || tas != sz )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_market_order(false, sz, ecb);

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>  -------  25 <4> 25 <5>
    // mid + 3                    \___  25 <6>
    // mid + 2    25 <3>  ---     \___  50 aon <7>
    // mid + 1               \    \___  50 aon <8>
    // mid        100 <2>  --\____\___  S100 <9> (should match)

    // ..

    // mid + 1
    // mid        50 <2>

    unsigned long long v = orderbook->volume();
    if( v!= sz*2.75 )
        return 4;

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != sz / 2 || as != 0 || ts != 0 )
        return 5;

    auto tsales = orderbook->time_and_sales();
    for(auto& t : tsales)
        out<< t << std::endl;

    return 0;

}


int
TEST_advanced_AON_11(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/4, ecb, aot );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/2, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb, aot);

    // mid + 4    200 aon <1>          25 <4> 25 <5>
    // mid + 3                         25 <6>
    // mid + 2    25 aon <3>           50 aon <7>
    // mid + 1                         50 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid|| ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz || as != sz/4 || tbs != sz || tas != (3*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != (2*sz +sz/4) || tas != sz )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid), sz/2, ecb);

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>  --------     25 <3> 25 <4>
    // mid + 3                     \______  25 <5>
    // mid + 2    25 aon <3>  ---     \___  50 aon <6>
    // mid + 1                   \    \___  50 aon <7>
    // mid        100 <2>        \____\___  S50 <8> (should match)

    // ..

    // mid + 1
    // mid        100 <2>

    unsigned long long v = orderbook->volume();
    if( v!= (2*sz +sz/4))
        return 4;

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != sz || as != 0 || ts != 0 )
        return 5;

    auto tsales = orderbook->time_and_sales();
    for(auto& t : tsales)
        out<< t << std::endl;

    return 0;

}


int
TEST_advanced_AON_12(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/4, ecb, aot );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/2, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb, aot);

    // mid + 4    200 aon <1>          25 <4> 25 <5>
    // mid + 3                         25 <6>
    // mid + 2    25 aon <3>           50 aon <7>
    // mid + 1                         50 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid|| ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz || as != sz/4 || tbs != sz || tas != (3*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != (2*sz + sz/4) || tas != sz )
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid), 3*sz/4, ecb);

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>  --------     25 <3> 25 <4>
    // mid + 3                     \______  25 <5>
    // mid + 2    25 aon <3>  ---     \___  50 aon <6>
    // mid + 1                   \    \___  50 aon <7>
    // mid        100 <2>      --\____\___  S75  <8> (should match)

    // ..

    // mid + 1
    // mid        75 <2>

    unsigned long long v = orderbook->volume();
    if( v!= (2*sz +sz/2) )
        return 4;

    bp = orderbook->bid_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != 3*sz/4 || as != 0 || ts != 0 )
        return 5;

    auto tsales = orderbook->time_and_sales();
    for(auto& t : tsales)
        out<< t << std::endl;

    return 0;

}


int
TEST_advanced_AON_13(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    auto aot = AdvancedOrderTicketAON::build();

    // limit aon limit
    orderbook->insert_limit_order(true, conv(mid+incr*4), 2*sz, ecb, aot);
    orderbook->insert_limit_order(true, conv(mid), sz, ecb );
    orderbook->insert_limit_order(true, conv(mid+incr*2), sz/4, ecb, aot );

    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*4), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*3), sz/4, ecb);
    orderbook->insert_limit_order(false, conv(mid+incr*2), sz/2, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz/2, ecb, aot);

    // mid + 4    200 aon <1>          25 <4> 25 <5>
    // mid + 3                         25 <6>
    // mid + 2    25 aon <3>           50 aon <7>
    // mid + 1                         50 aon <8>
    // mid        100 <2>

    double bp = orderbook->bid_price();
    double ap = orderbook->ask_price();
    if( bp != mid|| ap != mid+incr*3 )
        return 1;

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    if( bs != sz || as != sz/4 || tbs != sz || tas != (3*sz/4) )
        return 2;

    tbs = orderbook->total_aon_bid_size();
    tas = orderbook->total_aon_ask_size();
    if( tbs != (2*sz + sz/4) || tas != sz)
        return 3;

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    orderbook->insert_limit_order(false, conv(mid+incr), 3*sz/4, ecb);

    dump_orders(orderbook, out);
    orderbook->dump_aon_limits(out);
    dynamic_cast<ManagementInterface*>(orderbook)->dump_internal_pointers(out);

    // mid + 4    200 aon <1>  --------     25 <3> 25 <4>
    // mid + 3                     \______  25 <5>
    // mid + 2    25 aon <3>  ---     \___  50 aon <6>
    // mid + 1                   \    \___  50 aon <7>   S75 <8> (should leave 25)
    // mid        100 <2>        \____\__________________/

    // ..

    // mid + 1                   25 <8>
    // mid        100 <2>

    unsigned long long v = orderbook->volume();
    if( v!= 2*sz + sz/4 )
        return 4;

    bp = orderbook->bid_price();
    ap = orderbook->ask_price();
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_aon_size();
    if( bp != mid || bs != sz || ap != mid+incr || as != sz/4 || ts != 0 )
        return 5;

    auto tsales = orderbook->time_and_sales();
    for(auto& t : tsales)
        out<< t << std::endl;

    return 0;

}

#endif /* RUN_FUNCTIONAL_TESTS */
