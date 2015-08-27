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

#ifndef JO_0815_MARKET_MAKER
#define JO_0815_MARKET_MAKER

#include "types.hpp"
#include <random>
#include <vector>
#include <functional>

namespace NativeLayer{

namespace SimpleOrderbook{
class LimitInterface;
}

class MarketMakerBase{
  SimpleOrderbook::LimitInterface *_book;
  fill_callback_type _my_callback;
  bool _is_running;

public:
  MarketMakerBase(fill_callback_type fill_callback=&default_callback);
  virtual ~MarketMakerBase()
    {
    };

  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                     price_type incr);
  virtual void stop();

  static void default_callback(callback_msg msg,id_type id, price_type price,
                               size_type size);
};

class MarketMaker
    : public MarketMakerBase{
 /*
  * an object that provides customizable orderflow from asymmetric market info
  * uses SimpleOrderbook's LimitInterface
  */
  size_type _sz_low, _sz_high;

  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr, _distr2;
  static const clock_type::time_point seedtp;

public:
  typedef MarketMakerBase my_base_type;

  MarketMaker(size_type sz_low, size_type sz_high,
              fill_callback_type fill_callback=&my_base_type::default_callback);
  MarketMaker(const MarketMaker& mm );
  void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                  price_type incr);
};

typedef std::vector<MarketMakerBase> market_makers_type;
};
#endif
