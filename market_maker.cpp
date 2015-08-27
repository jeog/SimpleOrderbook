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
#include "types.hpp"
#include <chrono>

namespace NativeLayer{

MarketMakerBase::MarketMakerBase(fill_callback_type fill_callback)
  :
    _book(nullptr),
    _my_callback(fill_callback),
    _is_running(false)
  {
  }

void MarketMakerBase::start(SimpleOrderbook::LimitInterface *book,
                                    price_type implied,
                                    price_type incr)
{
  if(!book)
    std::invalid_argument("book can not be null(ptr)");
  else{
    this->_is_running = true;
    this->_book = book;
  }
}

void MarketMakerBase::stop()
{
  this->_is_running = false;
  this->_book = nullptr;
}

void MarketMakerBase::default_callback(callback_msg msg,
                                       id_type id,
                                       price_type price,
                                       size_type size)
{
  const char* cb_type;
  switch(msg){
  case fill:
    cb_type =  "MM FILL: ";
    break;
  case cancel:
    cb_type = "MM CANCEL: ";
    break;
  }
  std::cout<< cb_type <<' '<<id<<' '<<price<<' '<<size<<std::endl;
}



MarketMaker::MarketMaker(size_type sz_low, size_type sz_high,
                         fill_callback_type fill_callback)
  :
  my_base_type(fill_callback),
  _sz_low(sz_low),
  _sz_high(sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(sz_low, sz_high),
  _distr2(1, 5)
    {
    }

MarketMaker::MarketMaker(const MarketMaker& mm)
  :
  my_base_type(mm),
  _sz_low(mm._sz_low),
  _sz_high(mm._sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(mm._sz_low, mm._sz_high),
  _distr2(1, 5)
    {
    }

void MarketMaker::start(SimpleOrderbook::LimitInterface *book,
                        price_type implied, price_type incr)
{
  size_type mod, count, i;
  price_type price;

  my_base_type::start(book,implied,incr);

  mod = this->_distr2(this->_rand_engine);
  count = this->_distr2(this->_rand_engine);
  /* insert some random sell-limits */
  for( i = 0, price = implied + 1 ;
       i < count;
       price += mod * incr, ++i )
  {
    book->insert_limit_order(false,price,this->_distr(this->_rand_engine),
                             &MarketMaker::default_callback);
  }
  /* insert some random buy-limits */
  for( i = 0, price = implied - 1 ;
       i < count;
       price -= mod * incr, ++i )
  {
    book->insert_limit_order(true,price,this->_distr(this->_rand_engine),
                             &MarketMaker::default_callback);
  }
}

const clock_type::time_point MarketMaker::seedtp = clock_type::now();

};
