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
#include "performance.hpp"

#ifdef RUN_PERFORMANCE_TESTS

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <exception>
#include <iostream>
#include <iomanip>
#include <future>

namespace {

using namespace std;
using namespace sob;

typedef function<double(FullInterface*,int)> test_ty;
typedef map<int, map< int, double>> exec_results_ty;
typedef map<string, map<int, exec_results_ty> > total_results_ty;

const vector<int> NORDERS = {100, 1000, 10000, 100000, 1000000};
const int NRUNS = 9;
const int NTHREADS = 3;

const vector<proxy_info_ty>
proxies = {
    make_proxy_info<100>( {make_tuple(.0, 1.0),
                           make_tuple(.0, 10.0),
                           make_tuple(.0, 100.0),
                           make_tuple(.0, 1000.0),
                           make_tuple(.0, 10000.0)}
    )
};

const vector< pair<string, const test_ty> >
tests = {
        {"n_limits", TEST_n_limits},
        {"n_basics", TEST_n_basics},
        {"n_pulls", TEST_n_pulls},
        {"n_replaces", TEST_n_replaces}
};


exec_results_ty
exec_perf_tests(const test_ty& func, const proxy_info_ty& proxy_info);

double
exec_perf_test_async( const test_ty& func,
                      const vector<FullInterface*>& orderbooks,
                      int norders );

void
display_performance_results(const total_results_ty& results);

}; /* namespace */


const categories_ty performance_categories = {
        {"PERFORMANCE", run_performance_tests},
};

int
run_performance_tests()
{
    using namespace std;
    using namespace sob;

    total_results_ty results;

    cout<< "    NRUNS: " << NRUNS << endl
        << "    NTHREADS: " << NTHREADS << endl
        << "    NORDERS: ";

    for(int n : NORDERS)
        cout<< n << " ";
    cout<< endl;

    for( auto& test : tests ){
        string test_name = test.first;
        auto test_func = test.second;
        cout<< endl << "BEGIN TEST - " << test_name << endl << endl;

        for( auto& proxy_info : proxies ){
            int proxy_denom = get<0>(proxy_info);
            try{
                results[test_name][proxy_denom] =
                        exec_perf_tests(test_func, proxy_info);
            }catch(std::exception& e){
                cerr<< e.what() << endl;
                return 1;
            }
        }
        cout<< "END TEST - " << test.first << endl << endl;
    }

    streamsize old_precision = cout.precision();
    cout.precision(6);
    cout<< fixed << endl << endl;
    display_performance_results(results);
    cout<< endl << right;
    cout.precision(old_precision);
    return 0;
}


namespace{

exec_results_ty
exec_perf_tests(const test_ty& func, const proxy_info_ty& proxy_info)
{
    vector<FullInterface*> orderbooks;
    exec_results_ty results;

    int proxy_denom = get<0>(proxy_info);
    auto& proxy = get<1>(proxy_info);

    for( auto& args : get<2>(proxy_info) ){
        double min_price = get<0>(args);
        double max_price = get<1>(args);
        size_t nticks = proxy.ticks_in_range(min_price,max_price);

        for( int n : NORDERS ){
            cout<< "  PROXY 1/" << proxy_denom << " - NTICKS "
                << nticks << " - NORDERS " << n << "::: ";

            for( int i = 0; i < NRUNS; ++i ){
                orderbooks.push_back( proxy.create(min_price, max_price) );
            }

            results[nticks][n] = exec_perf_test_async(func, orderbooks, n);

            for( int i = 0; i < NRUNS; ++i){
                proxy.destroy(orderbooks[i]);
            }
            orderbooks.clear();
            cout<< endl;
        }
    }
    return results;
}


double
exec_perf_test_async( const test_ty& func,
                      const vector<FullInterface*>& orderbooks,
                      int norders )
{
    double time_total = 0;
    for( int i = 0; i < NRUNS; ){
        vector<future<double>> futs;
        int rmndr = NRUNS -i;
        for( int ii = 0; ii < min(NTHREADS, rmndr) ; ++ii, ++i ){
            FullInterface* ob = orderbooks[i];
            future<double> f = async(
                launch::async,
                [=](){ return func(ob, norders); }
            );
            futs.push_back(move(f));
        }
        for( auto& f : futs ){
            double t = f.get();
            time_total += t;
            cout<< t << " ";
            cout.flush();
        }
        futs.clear();
    }
    cout<< endl;
    return time_total / NRUNS;
}


void
display_performance_results(const total_results_ty& results)
{
    const size_t CW = 10;
    const size_t WTOTAL = (NORDERS.size() + 1) * 10 + 2;
    const string LPAD(CW, ' ');
    const size_t LPAD_SZ = LPAD.length();

    auto center = [](string s, int w){
        int pad = w - s.size();
        if( pad < 0 ){
            return s.substr(0, w);
        }else{
            return string(int(pad/2), ' ') + s;
        }
    };

    cout<< center("Total Runtime (seconds)", WTOTAL + LPAD_SZ) << endl << endl;

    for(auto& test : results){
        for(auto& proxy : test.second){
            cout<< test.first << " - 1/" << proxy.first << endl
                << LPAD << left << center("(norders)", WTOTAL) << endl << endl
                << LPAD << setw(CW) << "" << "| ";
            for(int n: NORDERS){
                cout<< setw(CW) << n;
            }
            cout<< endl << LPAD << string(CW, '-') << "|"
                << string(WTOTAL-CW-1, '-') << endl;
            size_t y_mid = static_cast<size_t>(proxy.second.size() / 2);
            size_t pos = 0;
            for( auto& ntick : proxy.second){
                cout<< setw(LPAD_SZ) << (pos == y_mid ? "(nticks)" : "");
                cout<< setw(CW) << ntick.first << "| ";
                for( auto& norder : ntick.second){
                    cout<< setw(CW) << norder.second;
                }
                cout<< endl;
                ++pos;
            }
            cout<< endl;
        }
        cout<< endl;
    }
}

}; /* namespace */

#endif /* RUN_PERFORMANCE_TESTS */


