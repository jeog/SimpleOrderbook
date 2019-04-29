/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include "functional.hpp"

#ifdef RUN_FUNCTIONAL_TESTS

#include <vector>
#include <tuple>
#include <iostream>
#include <iomanip>

namespace {

using namespace std;
using namespace sob;

const vector<proxy_info_ty>
proxies = {
    make_proxy_info<4>( {make_tuple(0.0,10.0), // need atleast 20 ticks
                         make_tuple(0.25,1000.0),
                         make_tuple(1000.0,1100.0)}
                       ),
    make_proxy_info<32>( {make_tuple(0.0,10.0),
                          make_tuple(1000, 1005)} ),
    make_proxy_info<10000>( {make_tuple(0.0,.1),
                             make_tuple(9.999, 10.01)} ),
    make_proxy_info<1000000>( {make_tuple(.000002, .000100)} )
};

const vector< pair<string, int(*)(FullInterface*)>>
orderbook_tests = {
      {"TEST_basic_orders_1", TEST_basic_orders_1},
      {"TEST_basic_orders_2", TEST_basic_orders_2},
      {"TEST_orders_info_pull_1", TEST_orders_info_pull_1},
      {"TEST_replace_order_1", TEST_replace_order_1},
      {"TEST_grow_1", TEST_grow_1},
      {"TEST_grow_2", TEST_grow_2},
      {"TEST_advanced_OCO_1", TEST_advanced_OCO_1},
      {"TEST_advanced_OCO_2", TEST_advanced_OCO_2},
      {"TEST_advanced_OCO_3", TEST_advanced_OCO_3},
      {"TEST_advanced_OCO_4", TEST_advanced_OCO_4},
      {"TEST_advanced_OTO_1", TEST_advanced_OTO_1},
      {"TEST_advanced_OTO_2", TEST_advanced_OTO_2},
      {"TEST_advanced_OTO_3", TEST_advanced_OTO_3},
      {"TEST_advanced_FOK_1", TEST_advanced_FOK_1},
      {"TEST_advanced_BRACKET_1", TEST_advanced_BRACKET_1},
      {"TEST_advanced_BRACKET_2", TEST_advanced_BRACKET_2},
      {"TEST_advanced_BRACKET_3", TEST_advanced_BRACKET_3},
      {"TEST_advanced_TRAILING_STOP_1", TEST_advanced_TRAILING_STOP_1},
      {"TEST_advanced_TRAILING_STOP_2", TEST_advanced_TRAILING_STOP_2},
      {"TEST_advanced_TRAILING_STOP_3", TEST_advanced_TRAILING_STOP_3},
      {"TEST_advanced_TRAILING_BRACKET_1", TEST_advanced_TRAILING_BRACKET_1},
      {"TEST_advanced_TRAILING_BRACKET_2", TEST_advanced_TRAILING_BRACKET_2},
      {"TEST_advanced_TRAILING_BRACKET_3", TEST_advanced_TRAILING_BRACKET_3},
      {"TEST_advanced_TRAILING_BRACKET_4", TEST_advanced_TRAILING_BRACKET_4},
      {"TEST_advanced_TRAILING_BRACKET_5", TEST_advanced_TRAILING_BRACKET_5},
};

const vector< pair<string, function<int(void)>>>
tick_price_tests = {
    {"Test_tick_price<1/4>", TEST_tick_price_1}
};

}; /* namespace */


const categories_ty functional_categories = {
        {"TICK_PRICE", run_tick_price_tests},
        {"ORDERBOOK", run_orderbook_tests}
};


int
run_tick_price_tests(int argc, char* argv[])
{
    using namespace std;

    for( auto& tests : tick_price_tests ){
        cout<< "** BEGIN - " << tests.first << " **" << endl;
        int err = tests.second();
        cout<< "** END - " << tests.first << " **" << endl << endl;
        cout<< (err == 0 ? "SUCCESS" : "FAILURE") << endl << endl;
        if(err){
            return err;
        }
    }
    return 0;
}


int
run_orderbook_tests(int argc, char* argv[])
{
    using namespace std;
    using namespace sob;

    for( auto& test : orderbook_tests ){
        for( auto& proxy_info : proxies ){
            auto& proxy = get<1>(proxy_info);
            auto& proxy_args = get<2>(proxy_info);

            for( auto& args : proxy_args ){
                double min_price = get<0>(args);
                double max_price = get<1>(args);

                stringstream test_head;
                test_head << test.first << " - 1/" << get<0>(proxy_info)
                          << " - " << min_price << "-" << max_price;
                cout<< "** BEGIN - " << test_head.str() << " **" << endl;

                FullInterface *orderbook = proxy.create(min_price, max_price);
                int err = test.second(orderbook);
                if( !err ){
                    proxy.destroy(orderbook);
                }

                cout<< "** END - " << test_head.str() << " **" << endl << endl;
                cout<< (err == 0 ? "SUCCESS" : "FAILURE") << endl << endl;

                if(err){
                    print_orderbook_state(orderbook);
                    proxy.destroy(orderbook);
                    return err;
                }
            }
        }
    }
    return 0;
}


void callback( sob::callback_msg msg,
               sob::id_type id1,
               sob::id_type id2,
               double price,
               size_t size)
{
    std::cout<< "CALLBACK ::  "
             << std::setw(22) << std::left << msg << " " << std::right
             << std::setw(5) << id1 << " "
             << std::setw(5) << id2 << " "
             << std::setw(12) << std::fixed << price << " "
             << std::setw(6) << size << std::endl;
    std::cout.unsetf(std::ios_base::floatfield);
}


void
print_orderbook_state( sob::FullInterface *ob,
                       size_t max_depth,
                       size_t max_timesales,
                       bool dump )
{
    using namespace std;

    cout<< "*** ORDERBOOK STATE ***" << endl
        << "min: " << ob->min_price() << endl
        << "max: " << ob->max_price() << endl
        << "incr: " << ob->tick_size() << endl
        << "bid: " << ob->bid_price() << endl
        << "ask: " << ob->ask_price() << endl
        << "last: " << ob->last_price() << endl
        << "bid size: " << ob->bid_size() << endl
        << "ask size: " << ob->ask_size() << endl
        << "last size: " << ob->last_size() << endl
        << "total bid size: " << ob->total_bid_size() << endl
        << "total ask size: " << ob->total_ask_size() << endl
        << "total size: " << ob->total_size() <<  endl
        << "volume: " << ob->volume() <<  endl
        << "last id: " << ob->last_id() <<  endl;

    cout << "market depth:" <<  endl;
    for( auto& p : ob->market_depth(max_depth) ){
        cout<< setw(8) << p.first << " "
            << p.second.first << " " << p.second.second <<  endl;
    }

    cout<< "time & sales:" <<  endl;
    auto ts = ob->time_and_sales();
    auto ts_beg = std::max(ts.cbegin(), ts.cend() - max_timesales);
    for( ; ts_beg < ts.cend() ; ++ts_beg){
        cout <<  setw(10) << sob::to_string(get<0>(*ts_beg))
             << " " << get<1>(*ts_beg) << " " << get<2>(*ts_beg) << endl;
    }

    if( dump ){
        dump_orders(ob);
    }
    cout<< "*** ORDERBOOK STATE ***" << endl;
}


void
dump_orders(sob::FullInterface *orderbook, std::ostream& out)
{
    orderbook->dump_sell_limits(out);
    orderbook->dump_buy_limits(out);
    orderbook->dump_buy_stops(out);
    orderbook->dump_sell_stops(out);
}


sob::order_exec_cb_type
create_advanced_callback(std::map<sob::id_type, sob::id_type>& ids)
{
    return [&](sob::callback_msg msg, sob::id_type id1, sob::id_type id2,
               double price, size_t size)
               {
                   if(id1 != id2 ){
                       for(auto& p : ids){
                           if( p.second == id1 ){
                               p.second = id2;
                               callback(msg,id1,id2,price,size);
                               return;
                           }
                       }
                   }
                   if( ids.find(id1) == ids.end() ){
                       ids[id1] = id2;
                   }
                   callback(msg,id1,id2,price,size);
               };
}


#endif /* RUN_FUNCTIONAL_TESTS */
