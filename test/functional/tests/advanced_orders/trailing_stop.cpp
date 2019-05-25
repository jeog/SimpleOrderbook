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

    inline void
    id_insert(id_type id)
    { ids[id] = id; }

    condition_trigger FULL_FILL = condition_trigger::fill_full;
}

int
TEST_advanced_TRAILING_STOP_1(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end) / 2);
    double incr = orderbook->tick_size();

    ids.clear();

    auto aot = AdvancedOrderTicketTrailingStop::build(10, FULL_FILL);
    id_type id1 = orderbook->insert_limit_order(true, mid, sz, ecb, aot);
    orderbook->insert_limit_order(false, conv(mid+incr), sz, ecb);
    orderbook->insert_limit_order(false, conv(mid+3*incr), sz, ecb);
    orderbook->insert_market_order(false, sz);
    dump_orders(orderbook,out);

    order_info oi = orderbook->get_order_info(ids[id1]);
    out<< "ORDER INFO: " << ids[id1] << " " << oi << endl;
    if( oi.stop != conv(mid-10*incr) ){
        return 1;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop != conv(mid-9*incr) ){
        return 2;
    }

    orderbook->insert_market_order(true, sz);
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop != conv(mid-7*incr) ){
        return 3;
    }

    return 0;
}


int
TEST_advanced_TRAILING_STOP_2(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;

    ids.clear();

    for(double d = conv(mid+incr); d <= end; d=conv(d+incr) ){
        orderbook->insert_limit_order(false, d, sz, ecb);
    }

    for(double d = conv(mid-incr); d >= beg; d=conv(d-incr) ){
        orderbook->insert_limit_order(true, d, sz, ecb);
    }

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr, FULL_FILL);
    id_type id1 = orderbook->insert_limit_order(false, mid, sz, ecb, aot);
    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);

    order_info oi = orderbook->get_order_info(ids[id1]);
    if( oi.stop ){
        return 1;
    }

    orderbook->insert_market_order(true, static_cast<size_t>(sz/2), ecb);
    oi = orderbook->get_order_info(ids[id1]);
    double exp_stop = conv(mid+stop_incr*incr);
    if( oi.stop != exp_stop ){
        return 1;
    }
    //
    //                mid + 5 (#5)
    //                mid + 2 (#4)
    //                mid + 1 (#1)
    // mid - 1 (#2)
    // mid - 2
    // mid - 4 (#3)
    // mid - 10 (#6)
    //

    double max_neg_price = mid;
    for(int i : {1,-1,-3, 1, 3, -6}){ // need pos to stat > -10
        orderbook->insert_market_order(bool(i >= 0), sz*abs(i) , ecb);
        orderbook->dump_stops(out);

        max_neg_price = min(orderbook->last_price(), max_neg_price);
        oi = orderbook->get_order_info(ids[id1]);
        exp_stop = conv(max_neg_price+stop_incr*incr);
        if( oi.stop != exp_stop){
            return 3;
        }
    }
    orderbook->dump_stops(out);

    if( oi.stop != mid ){
        return 4;
    }

    orderbook->insert_limit_order(false, mid, sz, ecb);
    orderbook->insert_market_order(true, sz);

    oi = orderbook->get_order_info(ids[id1]);
    if( oi ){
        return 5;
    }

    if( orderbook->last_price() != conv(mid+6*incr)){
        return 6;
    }

    return 0;
}

int
TEST_advanced_TRAILING_STOP_3(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    unsigned long long vol = 0;

    ids.clear();

    orderbook->insert_limit_order(true, beg, sz, ecb);
    orderbook->insert_limit_order(true, conv(beg+incr), sz*5, ecb);
    orderbook->insert_limit_order(false, end, sz*5, ecb);

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr, FULL_FILL);
    orderbook->insert_limit_order(true, mid, sz*2, ecb, aot);
    id_type id2 = orderbook->insert_stop_order(false, mid, sz, ecb, aot);
    //
    //  mid    200    S 100
    //  ..
    //  beg    500
    dump_orders(orderbook,out);

    // issues both trailing stops, but only the first trades
    orderbook->insert_limit_order(false, beg, sz*2, ecb);
    vol += 5*sz;
    // ...
    //  mid-10
    //  ..
    //  beg+10  S 100(c)
    //  ..
    //  beg     L 200(b)
    dump_orders(orderbook,out);

    if( orderbook->last_price() != conv(beg+incr) ){
        return 1;
    }else if(orderbook->volume() != vol ){
        return 2;
    }

    //
    //  beg+10   S 100(c)
    //  beg + 1  L 100
    //  beg      L 100

    if( orderbook->last_price() != conv(beg+incr) ){
        return 3;
    }else if( orderbook->volume() != vol ){
        return 4;
    }else if( orderbook->total_ask_size() != 5*sz ){
        return 5;
    }else if( orderbook->total_bid_size() != 3*sz ){
        return 6;
    }

    auto ts = orderbook->time_and_sales();
    if( ts.size() != 3 ){
        return 7;
    }
    if( get<2>(ts[2]) != sz*2){
        return 8;
    }else if( get<2>(ts[1]) != sz ){
        return 9;
    }else if( get<2>(ts[0]) != sz*2 ){
        return 10;
    }

    order_info oi = orderbook->get_order_info(ids[id2]);
    if( !oi || oi.stop != conv(beg + incr + stop_incr * incr) ){
        return 11;
    }

    orderbook->insert_limit_order(false, beg, sz*3, ecb);
    vol += 3 * sz;
    dump_orders(orderbook,out);

    oi = orderbook->get_order_info(ids[id2]);
    if( oi.stop != conv(beg + stop_incr * incr) ){
        return 12;
    }

    if( !orderbook->pull_order(ids[id2]) ){
        return 13;
    }
    dump_orders(orderbook,out);


    orderbook->insert_limit_order(true, end, sz);
    vol += sz;
    if( orderbook->volume() != vol ){
        return 14;
    }

    return 0;
}

int
TEST_advanced_TRAILING_STOP_4(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    size_t exp_vol = 0;

    ids.clear();

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr); // partial
    orderbook->insert_limit_order(true, mid, sz, ecb, aot);

    orderbook->insert_limit_order(false, mid, sz, ecb);
    exp_vol += sz;

    // mid - 10       S 100 stop

    dump_orders(orderbook,out);

    orderbook->insert_limit_order(false, mid+incr, sz, ecb);

    // mid + 1       S 100 limit
    // ...
    // mid - 10      S 100 stop

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook,out);

    // mid - 9       S 100 stop

    orderbook->insert_limit_order(true, conv(mid-8*incr), sz, ecb);
    orderbook->insert_limit_order(true, conv(mid-9*incr), 2*sz, ecb);

    // mid - 8       B 100 limit
    // mid - 9      B 200 limit     S 100 stop

    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += sz*3;

    dump_orders(orderbook,out);

    size_t bs, as, ts, vol;
    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    ts = orderbook->total_size();
    vol = orderbook->volume();

    if( bs != 0 || as != 0 || ts != 0)
        return 1;

    if( vol != exp_vol )
        return 2;

    dump_orders(orderbook,out);

    return 0;
}

int
TEST_advanced_TRAILING_STOP_5(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    size_t exp_vol = 0;

    ids.clear();

    id_type id_primary, id_loss, id;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_TRAILING_STOP_open:
            id_primary = id1;
            id_loss = id2;
            break;
        case callback_msg::trigger_TRAILING_STOP_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            if( id2 != id_loss )
                throw std::runtime_error("open-loss: id2 != id_loss");
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr); // partial
    id = orderbook->insert_limit_order(true, mid, sz*10, our_cb, aot);

    orderbook->insert_limit_order(true, beg, sz*11, ecb);
    orderbook->insert_limit_order(false, mid, sz, ecb);
    exp_vol += sz;

    // mid            B 900 limit
    //
    // mid - 10       S 100 stop
    //
    // beg            B 1100 limit

    dump_orders(orderbook,out);

    if( id != id_primary )
        return 1;

    auto so = orderbook->get_order_info( id_loss );
    if( so.is_buy )
        return 2;
    if( so.size != sz )
        return 3;
    if( so.stop != conv(mid-10*incr) )
        return 4;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    // mid            B 800 limit
    //
    // mid - 10       S 200 stop
    //
    // beg            B 1100 limit


    dump_orders(orderbook,out);

    so = orderbook->get_order_info( id_loss );
    if( so.is_buy )
        return 5;
    if( so.size != sz*2 )
        return 6;
    if( so.stop != conv(mid-10*incr) )
        return 7;

    orderbook->insert_limit_order(false, mid+incr, sz, ecb);

    // mid + 1        S 100 limit
    // mid            B 800 limit
    //
    // mid - 10       S 200 stop
    //
    // beg            B 1100 limit

    orderbook->insert_market_order(true, sz, ecb);
    exp_vol += sz;

    // mid            B 800 limit
    //
    // mid - 9        S 200 stop
    //
    // beg            B 1100 limit


    so = orderbook->get_order_info( id_loss );
    if( so.stop != conv(mid-9*incr) )
        return 8;

    orderbook->insert_limit_order(true, conv(mid-8*incr), sz, ecb);
    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook,out);

    // mid            B 700 limit
    //
    // mid - 8        B 100 limit
    // mid - 9        S 300 stop
    //
    // beg            B 1100 limit


    so = orderbook->get_order_info( id_loss );
    if( so.size != sz*3 )
        return 9;

    size_t bs = orderbook->bid_size();
    size_t tbs = orderbook->total_bid_size();
    size_t vol = orderbook->volume();
    if(bs != sz* 7 )
        return 10;
    if( tbs != sz*19 )
        return 11;
    if( vol != exp_vol )
        return 12;

    orderbook->insert_limit_order(false, beg, sz*8, ecb);
    exp_vol += sz*8;

    dump_orders(orderbook,out);

    // mid - 9        S 1000 stop
    //
    // beg            B 1100 limit

    so = orderbook->get_order_info( id_loss );
    if( so.size != sz*10 )
        return 13;
    if( so.stop != conv(mid-9*incr) )
        return 14;


    orderbook->insert_limit_order(true, conv(mid+2*incr), sz, ecb);
    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook,out);

    // mid - 8        S 1000 stop
    //
    // beg            B 1100 limit

    so = orderbook->get_order_info( id_loss );
    if( so.stop != conv(mid-8*incr) )
        return 14;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz*11;
    //

    if( orderbook->pull_order(id) )
        return 15;
    if( orderbook->pull_order(id_primary) )
        return 16;
    if( orderbook->pull_order(id_loss) )
        return 17;

    bs = orderbook->bid_size();
    size_t ts = orderbook->total_size();
    vol = orderbook->volume();
    if( bs != 0 || ts != 0 )
        return 18;
    if( vol != exp_vol )
        return 19;

    return 0;
}


int
TEST_advanced_TRAILING_STOP_6(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    size_t exp_vol = 0;

    ids.clear();

    id_type id_primary, id_loss;

    auto our_cb = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_TRAILING_STOP_open:
            id_primary = id1;
            id_loss = id2;
            break;
        case callback_msg::trigger_TRAILING_STOP_open_loss:
            if( id1 != id_primary )
                throw std::runtime_error("open-loss: id1 != id_primary");
            if( id2 != id_loss )
                throw std::runtime_error("open-loss: id2 != id_loss");
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    try{
        auto aot = AdvancedOrderTicketTrailingStop::build(1);
        orderbook->insert_limit_order(false, end, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 1;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto aot = AdvancedOrderTicketTrailingStop::build(1);
        orderbook->insert_limit_order(true, beg, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 2;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto n = orderbook->ticks_in_range(mid, end);
        auto aot = AdvancedOrderTicketTrailingStop::build(n + 1);
        orderbook->insert_stop_order(false, conv(mid+incr), mid, 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 3;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    try{
        auto n = orderbook->ticks_in_range(mid, end);
        auto aot = AdvancedOrderTicketTrailingStop::build(n + 1);
        orderbook->insert_stop_order(false, conv(mid), conv(mid+incr), 1, nullptr, aot);
        out << "failed to catch exception" << std::endl;
        return 4;
    }catch(std::exception& e){
        out << "successfully caught exception: " << e.what() << std::endl;
    }

    orderbook->insert_limit_order(true, mid, sz, ecb);
    orderbook->insert_limit_order(true, conv(mid-incr), sz, ecb);

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr); // partial
    orderbook->insert_limit_order(false, (mid-4*incr), sz*5, our_cb, aot);
    exp_vol += sz*2;

    dump_orders(orderbook, out);
    // mid + 9        B 200 stop
    // mid - 4        S 300 limit

    size_t bs = orderbook->bid_size();
    size_t as = orderbook->ask_size();
    size_t vol = orderbook->volume();
    if(bs != 0 || as != sz*3 )
        return 5;
    if( vol != exp_vol)
        return 6;

    auto oi = orderbook->get_order_info(id_loss);
    if( oi.size != sz*2 )
        return 7;
    if( !oi.is_buy )
        return 8;
    if( oi.stop != conv(mid+9*incr) )
        return 9;

    if( !orderbook->pull_order(id_loss ) )
        return 10;

    dump_orders(orderbook, out);

    if( orderbook->get_order_info(id_loss) )
        return 11;

    if( !orderbook->get_order_info(id_primary) )
        return 12;

    orderbook->insert_limit_order(false, end, sz*10, ecb);
    orderbook->insert_market_order(true, sz*2, ecb);
    exp_vol += sz*2;

    dump_orders(orderbook, out);

    // end            S 1000 limit
    // ...
    // mid + 6        B 200 stop
    // mid - 4        S 100 limit

    size_t tbs = orderbook->total_bid_size();
    size_t tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if(tbs != 0 || tas != sz*11)
        return 13;
    if(vol != exp_vol)
        return 14;

    id_type id_loss_old = id_loss;
    id_type id_primary_old = id_primary;

    aot = AdvancedOrderTicketTrailingStop::build(1); // partial
    if( !orderbook->replace_with_limit_order(id_primary_old, true, conv(beg+incr),
            sz, our_cb, aot) )
        return 15;

    dump_orders(orderbook, out);

    // end            S 1000 limit
    // ...
    // mid + 6        B 200 stop
    // mid - 4
    // ...
    // beg + 1        B 100 limit

    if( orderbook->get_order_info(id_primary_old) )
        return 16;
    oi = orderbook->get_order_info(id_loss_old);
    if( oi.size != sz*2 )
        return 17;
    if( oi.stop != conv(mid+6*incr) )
        return 18;

    bs = orderbook->bid_size();
    as = orderbook->ask_size();
    if( bs != sz || as != sz*10 )
        return 19;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook, out);

    // end            S 1000 limit
    // ...
    // beg + 11       B 200 stop
    // ...
    // beg            S 100 stop

    tbs = orderbook->total_bid_size();
    oi = orderbook->get_order_info(id_loss_old);
    if( oi.stop != conv(beg+11*incr) )
        return 20;

    oi = orderbook->get_order_info(id_loss);
    if( oi.size != sz )
        return 21;
    if( oi.stop != conv(beg) )
        return 22;

    if( orderbook->replace_with_market_order(id_primary, true, sz*10, ecb) )
        return 23;

    orderbook->insert_limit_order(false, conv(beg+12*incr), sz, ecb);

    // end            S 1000 limit
    // ...
    // beg + 12       S 100 limit
    // beg + 11       B 200 stop
    // ...
    // beg            S 100 stop

    id_loss_old = id_loss;

    orderbook->replace_with_limit_order(id_loss, true, end, sz*9, our_cb, aot);
    exp_vol += sz*11;

    dump_orders(orderbook, out);

    // end -1      S 900 stop

    oi = orderbook->get_order_info(id_loss);
    if( oi.size != sz*9 )
        return 24;
    if( oi.stop != conv(end-incr) )
        return 25;

    if( orderbook->get_order_info(id_primary) )
        return 26;

    tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( tbs != 0 || tas != 0 )
        return 27;

    if( vol != exp_vol )
        return 28;

    return 0;

}

int
TEST_advanced_TRAILING_STOP_7(FullInterface *orderbook, std::ostream& out)
{
    auto conv = [&](double d){ return orderbook->price_to_tick(d); };

    double beg = orderbook->min_price();
    double end = orderbook->max_price();
    double mid = conv((beg + end)/ 2);
    double incr = orderbook->tick_size();
    int stop_incr = 10;
    size_t exp_vol = 0;

    ids.clear();

    id_type id_primary1, id_primary2, id_loss1, id_loss2;

    auto our_cb1 = [&](callback_msg msg, id_type id1, id_type id2,
                     double p, size_t sz)
    {
        switch( msg ){
        case callback_msg::trigger_TRAILING_STOP_open:
                id_primary1 = id1;
                id_loss1 = id2;
            break;
        case callback_msg::trigger_TRAILING_STOP_open_loss:
            if( id1 != id_primary1)
                throw std::runtime_error("open-loss: id1 != id_primary");
            if( id2 != id_loss1  )
                throw std::runtime_error("open-loss: id2 != id_primary");
            break;
        default:
            break;
        };
        ecb(msg, id1, id2, p, sz);
    };

    auto our_cb2 = [&](callback_msg msg, id_type id1, id_type id2,
                       double p, size_t sz)
      {
          switch( msg ){
          case callback_msg::trigger_TRAILING_STOP_open:
                  id_primary2 = id1;
                  id_loss2 = id2;
              break;
          case callback_msg::trigger_TRAILING_STOP_open_loss:
              if( id1 != id_primary2)
                  throw std::runtime_error("open-loss: id1 != id_primary");
              if( id2 != id_loss2  )
                  throw std::runtime_error("open-loss: id2 != id_primary");
              break;
          default:
              break;
          };
          ecb(msg, id1, id2, p, sz);
      };

    orderbook->insert_limit_order(false, mid, sz, ecb);

    auto aot = AdvancedOrderTicketTrailingStop::build(stop_incr); // partial

    orderbook->insert_stop_order(true, mid, end, sz*2, our_cb1, aot);

    dump_orders(orderbook, out);

    //  mid    S 100 limit   B 200 stop/limit

    orderbook->insert_market_order(true, sz, our_cb2, aot);
    exp_vol += sz;

    dump_orders(orderbook, out);

    //  end             B 200 limit
    //  ...
    //  mid - 10        S 100 stop

    auto oi = orderbook->get_order_info(id_loss2);
    if( oi.size != sz )
        return 1;
    if( oi.is_buy )
        return 2;
    if( oi.stop != conv(mid-10*incr) )
        return 3;

    if( orderbook->get_order_info(id_primary2) )
        return 4;

    size_t bs = orderbook->bid_size();
    size_t vol = orderbook->volume();
    if( bs != sz*2 )
        return 5;
    if( vol != exp_vol )
        return 6;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz;

    dump_orders(orderbook, out);

    //  end             B 100 limit
    //  ...
    //  end - 10        S 100 stop   S 100 stop

    bs = orderbook->bid_size();
    vol = orderbook->volume();
    if( bs != sz )
        return 7;
    if( vol != exp_vol )
        return 8;

    oi = orderbook->get_order_info(id_loss1);
    if( oi.size != sz )
        return 9;
    if( oi.stop != conv(end-10*incr) )
        return 10;

    oi = orderbook->get_order_info(id_loss2);
    if( oi.size != sz )
        return 11;
    if( oi.stop != conv(end-10*incr) )
        return 12;

    orderbook->insert_limit_order(true, conv(end-9*incr), sz, ecb);
    orderbook->insert_limit_order(true, conv(end-10*incr), sz*4, ecb);

    dump_orders(orderbook, out);

    //  end             B 100 limit
    //  ...
    //  end - 9         B 100 limit
    //  end - 10        S 100 stop   S 100 stop  B 400 limit

    orderbook->insert_market_order(false, sz*2, ecb);
    exp_vol += sz*2;

    dump_orders(orderbook, out);

    //  end - 10        S 100 stop   S 200 stop  B 400 limit

    bs = orderbook->bid_size();
    size_t tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( bs != sz*4|| tas != 0 )
        return 13;
    if( vol != exp_vol )
        return 14;

    orderbook->insert_market_order(false, sz, ecb);
    exp_vol += sz*4;

    //

    size_t tbs = orderbook->total_bid_size();
    tas = orderbook->total_ask_size();
    vol = orderbook->volume();
    if( tbs != 0 || tas != 0 )
        return 15;
    if( vol != exp_vol )
        return 16;

    if( orderbook->get_order_info(id_primary1) )
        return 17;

    if( orderbook->get_order_info(id_loss1) )
        return 18;

    if( orderbook->get_order_info(id_primary2) )
        return 19;

    if( orderbook->get_order_info(id_loss2) )
        return 20;

    return 0;

}


#endif /* RUN_FUNCTIONAL_TESTS */


