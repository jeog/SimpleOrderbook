/*
Copyright (C) 2015 Jonathon Ogden     < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see http://www.gnu.org/licenses.
*/

#include "simple_orderbook.hpp"
#include <chrono>

namespace NativeLayer{


MarketMaker::MarketMaker(size_type sz_low, size_type sz_high)
  :
  _sz_low(sz_low),
  _sz_high(sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(sz_low, sz_high),
  _orderbook(nullptr)
    {
    }


MarketMaker::MarketMaker(const MarketMaker& mm)
  :
  _sz_low(mm._sz_low),
  _sz_high(mm._sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(mm._sz_low, mm._sz_high),
  _orderbook(nullptr)
    {
    }

limit_order_type MarketMaker::post_bid(price_type price)
{
  return limit_order_type(price, this->_distr(this->_rand_engine));
}

limit_order_type MarketMaker::post_ask(price_type price)
{
  return limit_order_type(price, this->_distr(this->_rand_engine));
}


void MarketMaker::default_callback(id_type id, price_type price, size_type size)
{
  std::cout<<"MM FILL: "<<' '<<id<<' '<<price<<' '<<size<<std::endl;
}


const clock_type::time_point MarketMaker::seedtp = clock_type::now();


std::ostream& operator<<(std::ostream& out, limit_order_type lim)
{
  std::cout<< lim.second << ',' << lim.first;
  return out;
}
std::ostream& operator<<(std::ostream& out, stop_order_type stp)
{
  std::cout<< stp.first << ',' << stp.second;  // chain to limit overload
  return out;
}

};
