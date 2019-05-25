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
TEST_advanced_BRACKET_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            conv(end-10*incr), conv(end-12*incr), conv(end-6*incr)
            );

    out<< "AdvancedOrderTicketBRACKET: " << aot << endl;
    out<< "OrderParamaters 1: " << *aot.order1() << endl;
    out<< "OrderParamaters 2: " << *aot.order2() << endl;

    orderbook->insert_limit_order(true, conv(end-8*incr), sz, ecb, aot);
    orderbook->insert_market_order(false, sz);
    // end - 6      L 100
    // end - 8
    // end - 10     S 100
    dump_orders(orderbook, out);

    if( orderbook->volume() != sz ){
        return 1;
    }else if( orderbook->ask_price() != conv(end-6*incr) ){
        return 2;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook, out);

    if( orderbook->volume() != 2*sz ){
        return 3;
    }else if( orderbook->total_ask_size() != 0 ){
        return 4;
    }

    return 0;
}


int
TEST_advanced_BRACKET_2(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(false, beg, 2*sz, ecb);

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            conv(end-10*incr), conv(end-12*incr), conv(end-6*incr)
            );
    dump_orders(orderbook, out);

    orderbook->insert_limit_order(true, conv(end-8*incr), sz, ecb, aot);
    // end - 6      L 100
    // end - 8
    // end - 10     S 100
    // ..
    // beg          L 100
    dump_orders(orderbook, out);

    if( orderbook->volume() != sz ){
        return 1;
    }else if( orderbook->total_ask_size() != 2*sz ){
        return 2;
    }

    orderbook->insert_market_order(true, sz, ecb);
    //
    // end - 12     L 100
    //
    dump_orders(orderbook, out);

    orderbook->insert_limit_order(true, end, 2*sz, ecb);
    dump_orders(orderbook, out);

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
TEST_advanced_BRACKET_3(FullInterface *orderbook, std::ostream& out)
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
            conv(mid+10*incr), conv(mid-10*incr)
            );
    orderbook->insert_limit_order(false, mid, sz, ecb, aot2);
    //
    // beg         L 100
    //
    dump_orders(orderbook,out);

    // NOTE the initial OTO limit is inserted BEFORE the BRACKET limit
    if( orderbook->volume() != static_cast<unsigned long>(2 * sz) ){
        return 1;
    }else if( get<1>(orderbook->time_and_sales().back()) != beg ){
        return 2;
    }else if( orderbook->total_ask_size() !=0 ){
        return 3;
    }

    return 0;
}

int
TEST_advanced_BRACKET_4(FullInterface *orderbook, std::ostream& out)
{

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            mid -incr, mid-incr*2, mid+incr
            );
    orderbook->insert_limit_order(true, mid, sz* 10, ecb, aot);

    // mid    B 10 limit

    dump_orders(orderbook, out);

    orderbook->insert_limit_order(false, mid, sz, ecb);

    // mid + 1    S 1 limit
    // mid        B 9 limit
    // mid - 1    S 1 stop

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    size_t exp_vol = sz;
    auto md = orderbook->market_depth();
    if( bs != sz*9 )
        return 1;
    if( as != sz )
        return 2;
    if( vol != exp_vol )
        return 3;


    orderbook->insert_limit_order(false, mid, sz*4, ecb);
    exp_vol += (sz*4);

    // mid + 1    S 5 limit
    // mid        B 5 limit
    // mid - 1    S 5 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*5 )
        return 4;
    if( as != sz* 5 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_limit_order(false, mid, sz*5, ecb);
    exp_vol += (sz*5);

     // mid + 1    S 10 limit
     // mid
     // mid - 1    S 10 stop

     dump_orders(orderbook, out);

     bs = orderbook->bid_size();
     as = orderbook->ask_size();
     vol = orderbook->volume();
     if( bs != 0 )
         return 7;
     if( as != sz* 10 )
         return 8;
     if( vol != exp_vol )
         return 9;

     orderbook->insert_market_order(true, sz, ecb);
     exp_vol += sz;

     // mid + 1    S 9 limit
     // mid
     // mid - 1    S 9 stop

     dump_orders(orderbook, out);

     bs = orderbook->bid_size();
     as = orderbook->ask_size();
     vol = orderbook->volume();
     if( bs != 0 )
         return 10;
     if( as != sz* 9 )
         return 11;
     if( vol != exp_vol )
         return 12;

     orderbook->insert_limit_order(true, conv(mid-2*incr), sz*10, ecb);

     // mid + 1    S 9 limit
     // mid
     // mid - 1    S 9 stop
     // mid - 2    B 10 limit

     dump_orders(orderbook, out);

     orderbook->insert_market_order(false, sz, ecb);
     exp_vol += (sz*10);

     // mid

     dump_orders(orderbook, out);

     bs = orderbook->total_bid_size();
     as = orderbook->total_ask_size();
     vol = orderbook->volume();
     if( bs != 0 )
         return 13;
     if( as != 0 )
         return 14;
     if( vol != exp_vol )
         return 15;


    return 0;
}

int
TEST_advanced_BRACKET_5(FullInterface *orderbook, std::ostream& out)
{

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(false, mid,sz, ecb);

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            mid -incr, mid-incr*2, mid+incr
            );
    orderbook->insert_limit_order(true, mid, sz* 10, ecb, aot);
    size_t exp_vol = sz;

    // mid +1  S 1 limit
    // mid     B 9 limit
    // mid +2  S 1 stop

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    auto md = orderbook->market_depth();
    if( bs != sz*9 )
        return 1;
    if( as != sz )
        return 2;
    if( vol != exp_vol )
        return 3;


    orderbook->insert_limit_order(false, mid, sz*4, ecb);
    exp_vol += (sz*4);

    // mid + 1    S 5 limit
    // mid        B 5 limit
    // mid - 1    S 5 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*5 )
        return 4;
    if( as != sz* 5 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    // mid + 1    S 4 limit
    // mid        B 5 limit
    // mid - 1    S 4 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*5 )
        return 7;
    if( as != sz* 4 )
        return 8;
    if( vol != exp_vol )
        return 9;

    orderbook->insert_limit_order(true, conv(mid-incr*2), sz*2, ecb );

    // mid + 1    S 4 limit
    // mid        B 5 limit
    // mid - 1    S 4 stop
    // mid - 2    B 2 limit

    orderbook->insert_limit_order(false, mid, sz*4, ecb);
    exp_vol += (sz*4);

    // mid + 1    S 8 limit
    // mid        B 1 limit
    // mid - 1    S 8 stop
    // mid - 2    B 2 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( tbs != sz*3 || bs != sz )
        return 10;
    if( as != sz* 8 )
        return 11;
    if( vol != exp_vol )
        return 12;

    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += (sz*3);

    // mid + 1    S 8 limit  (a) + 1           (c) - 9
    // mid        B 1 limit  (a) - 1
    // mid - 1    S 8 stop   (a) + 1           (c) - 9
    // mid - 2    B 2 limit           (b) - 1  (c) - 1, + 8

    // ...

    // mid + 1
    // mid
    // mid - 1
    // mid - 2    S 8 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    size_t ts = orderbook->total_size();
    if( bs != 0)
        return 13;
    if( as != sz*8 )
        return 14;
    if( ts != as )
        return 15;
    if( vol != exp_vol )
        return 16;

    orderbook->insert_limit_order(true, beg, 1);
    orderbook->insert_limit_order(false, end, 1);
    orderbook->insert_market_order(true, (sz*8)+1);
    orderbook->insert_market_order(false, 1);
    exp_vol += ((sz*8) + 2);

    ts = orderbook->total_size();
    vol = orderbook->volume();
    if( ts != 0 )
        return 17;
    if( vol != exp_vol )
        return 18;



    return 0;
}


int
TEST_advanced_BRACKET_6(FullInterface *orderbook, std::ostream& out)
{

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(false, mid,sz, ecb);

    auto aot = AdvancedOrderTicketBRACKET::build_sell_stop_limit(
            mid -incr, mid-incr*2, mid+incr, condition_trigger::fill_full
            );
    orderbook->insert_limit_order(true, mid, sz* 10, ecb, aot);
    size_t exp_vol = sz;

    // mid +1
    // mid     B 9 limit
    // mid +2

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    auto md = orderbook->market_depth();
    if( bs != sz*9 )
        return 1;
    if( as != 0 )
        return 2;
    if( vol != exp_vol )
        return 3;


    orderbook->insert_limit_order(false, mid, sz*5, ecb);
    exp_vol += (sz*5);

    // mid + 1
    // mid        B 4 limit
    // mid - 1

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*4 )
        return 4;
    if( as != 0 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_limit_order(true, mid, sz*5, ecb);

    // mid + 1
    // mid        B 4 limit   B 5 limit
    // mid - 1

    orderbook->insert_market_order(false, sz*4, ecb);
    exp_vol += (sz*4);

    // mid + 1    S 4 limit
    // mid        B 5 limit
    // mid - 1    S 4 stop

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*5 )
        return 7;
    if( as != sz* 4 )
        return 8;
    if( vol != exp_vol )
        return 9;

    orderbook->insert_limit_order(true, conv(mid-incr*2), sz*2, ecb );

    // mid + 1    S 4 limit
    // mid        B 5 limit
    // mid - 1    S 4 stop
    // mid - 2    B 2 limit

    orderbook->insert_limit_order(false, mid, sz*4, ecb);
    exp_vol += (sz*4);

    // mid + 1    S 4 limit
    // mid        B 1 limit
    // mid - 1    S 4 stop
    // mid - 2    B 2 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( tbs != sz*3 || bs != sz )
        return 10;
    if( as != sz* 4 )
        return 11;
    if( vol != exp_vol )
        return 12;

    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += (sz*3);

    // mid + 1    S 4 limit  (a)               (c) - 4
    // mid        B 1 limit  (a) - 1
    // mid - 1    S 4 stop   (a)               (c) - 4
    // mid - 2    B 2 limit           (b) - 1  (c) - 1, + 3

    // ...

    // mid + 1
    // mid
    // mid - 1
    // mid - 2    S 3 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    size_t ts = orderbook->total_size();
    if( bs != 0)
        return 13;
    if( as != sz*3 )
        return 14;
    if( ts != as )
        return 15;
    if( vol != exp_vol )
        return 16;

    orderbook->insert_limit_order(true, beg, 1);
    orderbook->insert_limit_order(false, end, 1);
    orderbook->insert_market_order(true, (sz*3)+1);
    orderbook->insert_market_order(false, 1);
    exp_vol += ((sz*3) + 2);

    ts = orderbook->total_size();
    vol = orderbook->volume();
    if( ts != 0 )
        return 17;
    if( vol != exp_vol )
        return 18;



    return 0;
}


int
TEST_advanced_BRACKET_7(FullInterface *orderbook, std::ostream& out)
{

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    orderbook->insert_limit_order(true, mid,sz, ecb);

    auto aot = AdvancedOrderTicketBRACKET::build_buy_stop(
            end, beg, condition_trigger::fill_partial
            );
    id_type id = orderbook->insert_limit_order(false, mid, sz* 10, ecb, aot);
    size_t exp_vol = sz;

    // end     B 1 stop
    // mid     S 9 limit
    // beg     B 1 limit

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    if( bs != sz )
        return 1;
    if( as != sz*9 )
        return 2;
    if( vol != exp_vol )
        return 3;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += (sz);

    // end
    // mid     S 9 limit
    // beg

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0 )
        return 4;
    if( as != sz*9 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_limit_order(true, mid, sz*2, ecb);
    exp_vol += (sz*2);

    // end        B 2 stop
    // mid        S 7 limit
    // beg        B 2 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz*2 )
        return 7;
    if( as != sz* 7 )
        return 8;
    if( vol != exp_vol )
        return 9;

    orderbook->insert_limit_order(false, beg, sz, ecb);
    exp_vol += (sz);

    // end        B 1 stop
    // mid        S 7 limit
    // beg        B 1 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( tbs != sz || bs != sz )
        return 10;
    if( as != sz* 7 )
        return 11;
    if( vol != exp_vol )
        return 12;


    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += (sz);

    // end        B 2 stop
    // mid        S 6 limit
    // beg        B 2 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    size_t ts = orderbook->total_size();
    if( bs != sz*2)
        return 13;
    if( as != sz*6 )
        return 14;
    if( ts != (bs+as) )
        return 15;
    if( vol != exp_vol )
        return 16;

    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += (sz*2);

    // end
    // mid        S 6 limit
    // beg

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0 )
        return 17;
    if( as != sz* 6 )
        return 18;
    if( vol != exp_vol )
        return 19;

    order_info oi = orderbook->get_order_info(id);
    if(oi.is_buy)
        return 20;
    if(oi.advanced.condition() != order_condition::bracket )
        return 21;
    if(oi.size != sz*6)
        return 20;

    bool res = orderbook->pull_order(id);
    if( !res )
        return 21;

   ts = orderbook->total_size();
   if( ts != 0 )
       return 22;

   vol = orderbook->volume();
   if( vol != exp_vol )
       return 23;

   auto md = orderbook->market_depth( (end-beg)/incr/2 );
   if( md.size() > 0 )
       return 24;

    return 0;
}

int
TEST_advanced_BRACKET_8(FullInterface *orderbook, std::ostream& out)
{

    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg+end) / 2);

    ids.clear();

    id_type id_primary, id_target, id_loss, id;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_BRACKET_open:
            id_primary = id1;
            id_target = id2;
            break;
        case callback_msg::trigger_BRACKET_open_target:
            if( id1 != id_primary )
                throw std::runtime_error("open-target: id1 != id_primary");
            if( id2 != id_target )
                throw std::runtime_error("open-target: id2 != id_target");
            break;
        case callback_msg::trigger_BRACKET_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            id_loss = id2;
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    // PULL PRIMARY TEST

    orderbook->insert_limit_order(true, mid,sz, our_cb);

    auto aot = AdvancedOrderTicketBRACKET::build_buy_stop(
            end, beg, condition_trigger::fill_partial
            );
    try{
        id = orderbook->insert_limit_order(false, mid, sz* 10, our_cb, aot);
    }catch(std::exception& exc){
        std::cerr<< "EXCEPTION: " << exc.what() << std::endl;
        return 1;
    }
    if( id != id_primary)
        return 2;

    size_t exp_vol = sz;

    // end     B 1 stop
    // mid     S 9 limit
    // beg     B 1 limit

    dump_orders(orderbook, out);

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    if( bs != sz )
        return 3;
    if( as != sz*9 )
        return 4;
    if( vol != exp_vol )
        return 5;

    if( !orderbook->pull_order(id_primary) )
        return 6;

    // end     B 1 stop
    // mid
    // beg     B 1 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz )
        return 7;
    if( as != 0 )
        return 8;
    if( vol != exp_vol )
        return 9;

    orderbook->insert_market_order(false, sz, our_cb);
    exp_vol += sz;

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    size_t ts = orderbook->total_size();
    vol = orderbook->volume();
    if( bs != 0 )
        return 10;
    if( as != 0 )
        return 11;
    if( ts != (bs + as) )
        return 12;
    if( vol != exp_vol )
        return 13;

    // PULL TARGET/LOSS TEST

    orderbook->insert_limit_order(true, mid,sz, our_cb);

    try{
        id = orderbook->insert_limit_order(false, mid, sz* 10, our_cb, aot);
    }catch(std::exception& exc){
        std::cerr<< "EXCEPTION: " << exc.what() << std::endl;
        return 1;
    }
    if( id != id_primary)
        return 2;

    exp_vol += sz;

    // end     B 1 stop
    // mid     S 9 limit
    // beg     B 1 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz )
        return 3;
    if( as != sz*9 )
        return 4;
    if( vol != exp_vol )
        return 5;

    if( !orderbook->pull_order(id_target) )
        return 6;

    if( orderbook->pull_order(id_loss) )
        return 7;

    // end
    // mid     S 9 limit
    // beg

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0)
        return 8;
    if( as != sz*9 )
        return 9;
    if( vol != exp_vol )
        return 10;

    orderbook->insert_market_order(true, sz, our_cb);
    exp_vol += sz;

    // end     B 1 stop
    // mid     S 8 limit
    // beg     B 1 limit

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != sz )
        return 11;
    if( as != sz*8 )
        return 12;
    if( vol != exp_vol )
        return 13;

    if( !orderbook->pull_order(id_loss) )
        return 14;

    // end
    // mid     S 8 limit
    // beg

    dump_orders(orderbook, out);

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    vol = orderbook->volume();
    if( bs != 0 )
        return 15;
    if( as != sz*8 )
        return 16;
    if( vol != exp_vol )
        return 17;

    orderbook->insert_market_order(true, sz, our_cb);
    exp_vol += sz;

    // end     B 1 stop
    // mid     S 7 limit
    // beg     B 1 limit

    dump_orders(orderbook, out);

    if( !orderbook->pull_order(id_primary) )
        return 18;

    if( !orderbook->pull_order(id_target) )
        return 19;

    orderbook->insert_limit_order(true, beg, 1);
    orderbook->insert_limit_order(false, end, 1);
    orderbook->insert_market_order(true, 1);
    orderbook->insert_market_order(false, 1);
    exp_vol += 2;

    ts = orderbook->total_size();
    vol = orderbook->volume();
    if( ts != 0 )
        return 20;
    if( vol != exp_vol )
        return 21;

    return 0;
}


#endif /* RUN_FUNCTIONAL_TESTS */

