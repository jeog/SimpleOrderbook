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

#include <random>

#include "../include/simpleorderbook.hpp"

using namespace sob;

namespace {

std::random_device rand_device;
std::default_random_engine random_engine( rand_device() );

}; /* namespace */


std::vector<double>
generate_prices(const FullInterface *ob, double min, double max, int n)
{
    double mu = (max + min) / 2;
    double sd = (max-min) / 20;
    std::normal_distribution<double> price_distribution(mu, sd);
    std::vector<double> prices;
    double r, rr;
    for( int i = 0; i < n; ++ i ){
        r = price_distribution(random_engine);
        rr = std::min( std::max(r,min), max);
        prices.push_back( ob->price_to_tick(rr) );
    }
    return prices;
}


std::vector<size_t>
generate_sizes(size_t min, size_t max, int n)
{
    std::lognormal_distribution<double> size_distribution(0,1);
    std::vector<size_t> sizes;
    size_t r;
    for( int i = 0; i < n; ++i ){
        r = static_cast<size_t>(size_distribution(random_engine) * 200);
        sizes.push_back( std::min( std::max(r,min), max) );
    }
    return sizes;
}


std::vector<bool>
generate_buy_sells(int n)
{
    std::bernoulli_distribution bool_distribution(.5);
    std::vector<bool> bs;
    for(int i = 0; i < n; ++i){
        bs.push_back( bool_distribution(random_engine) );
    }
    return bs;
}


std::vector<order_type>
generate_limit_market_stop(int n, int limit_ratio)
{
    std::uniform_int_distribution<int> order_distribution(1,4+(limit_ratio-1));
    std::vector<order_type> ots;
    for(int i = 0; i < n; ++i){
        int order_int = order_distribution(random_engine);
        if( order_int > 4 ){
            ots.push_back( order_type::limit );
        }else{
            ots.push_back( order_type(order_int) );
        }
    }
    return ots;
}


std::vector<order_type>
generate_limit_stop(int n, int limit_ratio)
{
    std::uniform_int_distribution<int> order_distribution(2,4+(limit_ratio-1));
    std::vector<order_type> ots;
    for(int i = 0; i < n; ++i){
        int order_int = order_distribution(random_engine);
        if( order_int > 4 ){
            ots.push_back( order_type::limit );
        }else{
            ots.push_back( order_type(order_int) );
        }
    }
    return ots;
}

#endif /* RUN_PERFORMANCE_TESTS */


