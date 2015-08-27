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

void MarketMaker::start(SimpleOrderbook::LimitInterface *book,
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

void MarketMaker::stop()
{
  this->_is_running = false;
  this->_book = nullptr;
}

void MarketMaker::bid(price_type price, size_type size) const
{
  this->_book->insert_limit_order( true, price, size, this->_callback);
}

void MarketMaker::offer(price_type price, size_type size) const
{
  this->_book->insert_limit_order( false, price, size, this->_callback);
}

void MarketMaker::default_callback(callback_msg msg,
                                   id_type id,
                                   price_type price,
                                   size_type size)
{
  const char* cb_type;
  switch(msg){
  case callback_msg::fill:
    cb_type =  "MM FILL: ";
    break;
  case callback_msg::cancel:
    cb_type = "MM CANCEL: ";
    break;
  case callback_msg::stop:
    cb_type = "MM STOP: ";
    break;
  }
  std::cout<< cb_type <<' '<<id<<' '<<price<<' '<<size<<std::endl;
}


/***/


MarketMaker_Random::MarketMaker_Random(size_type sz_low,
                                       size_type sz_high,
                                       callback_type callback)
  :
    my_base_type(callback),
    _sz_low(sz_low),
    _sz_high(sz_high),
    _rand_engine(this->_gen_seed()),
    _distr(sz_low, sz_high),
    _distr2(1, 5)
  {
  }

MarketMaker_Random::MarketMaker_Random(const MarketMaker_Random& mm)
  :
    my_base_type(mm),
    _sz_low(mm._sz_low),
    _sz_high(mm._sz_high),
    _rand_engine(this->_gen_seed()),
    _distr(mm._sz_low, mm._sz_high),
    _distr2(1, 5)
  {
  }

unsigned long long MarketMaker_Random::_gen_seed()
{
  return (clock_type::now() - MarketMaker_Random::seedtp).count()
         * (unsigned long long)this
         % std::numeric_limits<long>::max();
}

void MarketMaker_Random::start(SimpleOrderbook::LimitInterface *book,
                               price_type implied,
                               price_type incr)
{
  size_type mod, count, i;
  price_type price;

  my_base_type::start(book,implied,incr);

  mod = this->_distr2(this->_rand_engine);
  count = this->_distr2(this->_rand_engine);
  /* insert some random sell-limits */
  for( i = 0, price = implied + 1 ; i < count; price += mod * incr, ++i )
    this->offer(price, this->_distr(this->_rand_engine));
  /* insert some random buy-limits */
  for( i = 0, price = implied - 1 ; i < count; price -= mod * incr, ++i )
    this->bid(price, this->_distr(this->_rand_engine));
}

const clock_type::time_point MarketMaker_Random::seedtp = clock_type::now();

};
