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

const vector< pair<string, int(*)(FullInterface*, std::ostream&)>>
orderbook_tests = {
      {"TEST_basic_orders_1", TEST_basic_orders_1},
      {"TEST_basic_orders_2", TEST_basic_orders_2},
      {"TEST_orders_info_pull_1", TEST_orders_info_pull_1},
      {"TEST_replace_order_1", TEST_replace_order_1},
      {"TEST_grow_1", TEST_grow_1},
      {"TEST_grow_2", TEST_grow_2} ,
      {"TEST_advanced_AON_1", TEST_advanced_AON_1},
      {"TEST_advanced_AON_2", TEST_advanced_AON_2},
      {"TEST_advanced_AON_3", TEST_advanced_AON_3},
      {"TEST_advanced_AON_4", TEST_advanced_AON_4},
      {"TEST_advanced_AON_5", TEST_advanced_AON_5},
      {"TEST_advanced_AON_6", TEST_advanced_AON_6},
      {"TEST_advanced_AON_7", TEST_advanced_AON_7},
      {"TEST_advanced_AON_8", TEST_advanced_AON_8},
      {"TEST_advanced_AON_9", TEST_advanced_AON_9},
      {"TEST_advanced_AON_10", TEST_advanced_AON_10},
      {"TEST_advanced_AON_11", TEST_advanced_AON_11},
      {"TEST_advanced_AON_12", TEST_advanced_AON_12},
      {"TEST_advanced_AON_13", TEST_advanced_AON_13},
      {"TEST_advanced_OCO_1", TEST_advanced_OCO_1},
      {"TEST_advanced_OCO_2", TEST_advanced_OCO_2},
      {"TEST_advanced_OCO_3", TEST_advanced_OCO_3},
      {"TEST_advanced_OCO_4", TEST_advanced_OCO_4},
      {"TEST_advanced_OCO_5", TEST_advanced_OCO_5},
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

const vector< pair<string, int(*)(std::ostream&)>>
tick_price_tests = {
    {"Test_tick_price<1/4>", TEST_tick_price_1}
};

struct DummyOut : public std::ostream {
    template<typename T>
    DummyOut&
    operator<< (T){ return *this; }
} dout;
std::ofstream fout;
std::reference_wrapper<std::ostream> out = cout;
bool out_is_cout = true;

void
set_ostream(int argc, char* argv[] )
{
    if( argc == 1 ){
        out = dout;
        out_is_cout = false;
    }else if( argc > 1 ){
        if( std::string(argv[1]) != "-" ){
            if( !fout.is_open() ){
                fout.open(argv[1]);
                if( !fout )
                    throw std::invalid_argument(string(argv[1]) + " not a valid file");
            }
            out = fout;
            out_is_cout = false;
        }else{
            out = cout;
            out_is_cout = true;
        }
    }
}


}; /* namespace */


const categories_ty functional_categories = {
        {"TICK_PRICE", run_tick_price_tests},
        {"ORDERBOOK", run_orderbook_tests}
};



int
run_tick_price_tests(int argc, char* argv[])
{
    using namespace std;

    set_ostream(argc, argv);

    for( auto& tests : tick_price_tests ){
        if( !out_is_cout ){
            cout << "** " << tests.first << " ** ";
            cout.flush();
        }
        out.get() << "** BEGIN - " << tests.first << " **" << endl;

        int err = tests.second(out.get());

        if( !out_is_cout )
            cout << (err == 0 ? "SUCCESS" : "FAILURE") << endl;
        out.get() << "** END - " << tests.first << " **" << endl << endl;
                    out.get() << (err == 0 ? "SUCCESS" : "FAILURE") << endl << endl;

        if(err)
            return err;
    }
    return 0;
}


int
run_orderbook_tests(int argc, char* argv[])
{
    using namespace std;
    using namespace sob;

    set_ostream(argc, argv);

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

                if( !out_is_cout ){
                    cout << "** " << test_head.str() << " ** ";
                    cout.flush();
                }
                out.get() << "** BEGIN - " << test_head.str() << " **" << endl;

                FullInterface *orderbook = proxy.create(min_price, max_price);

                int err = test.second(orderbook, out.get());
                if( !err ){
                    proxy.destroy(orderbook);
                }

                if( !out_is_cout )
                    cout << (err == 0 ? "SUCCESS" : "FAILURE") << endl;
                out.get()<< "** END - " << test_head.str() << " **" << endl << endl;
                out.get()<< (err == 0 ? "SUCCESS" : "FAILURE") << endl << endl;


                if(err){
                    print_orderbook_state(orderbook, out.get());
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
    out.get() << "CALLBACK ::  "
             << std::setw(22) << std::left << msg << " " << std::right
             << std::setw(5) << id1 << " "
             << std::setw(5) << id2 << " "
             << std::setw(12) << std::fixed << price << " "
             << std::setw(6) << size << std::endl;
    out.get().unsetf(std::ios_base::floatfield);
}


void
print_orderbook_state( sob::FullInterface *ob,
                       std::ostream& out,
                       size_t max_depth,
                       size_t max_timesales,
                       bool dump )
{
    using namespace std;

    out<< "*** ORDERBOOK STATE ***" << endl
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
        << "total aon bid size: " << ob->total_aon_bid_size() << endl
        << "total aon ask size: " << ob->total_aon_ask_size() << endl
        << "total aon size: " << ob->total_aon_size() << endl
        << "last id: " << ob->last_id() <<  endl;

    out << "market depth:" <<  endl;
    for( auto& p : ob->market_depth(max_depth) ){
        out<< setw(8) << p.first << " "
            << p.second.first << " " << p.second.second <<  endl;
    }

    out << "aon market depth:" << endl;
    for( auto& p : ob->aon_market_depth() ){
        out<< setw(8) << p.first << " " << p.second.first << " "
            << p.second.second << endl;
    }

    out<< "time & sales:" <<  endl;
    auto ts = ob->time_and_sales();
    auto ts_beg = std::max(ts.cbegin(), ts.cend() - max_timesales);
    for( ; ts_beg < ts.cend() ; ++ts_beg){
        out <<  setw(10) << sob::to_string(get<0>(*ts_beg))
             << " " << get<1>(*ts_beg) << " " << get<2>(*ts_beg) << endl;
    }

    if( dump ){
        dump_orders(ob, out);
    }
    out<< "*** ORDERBOOK STATE ***" << endl;
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
