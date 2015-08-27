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

using namespace std::placeholders;

class MarketMaker{
  SimpleOrderbook::LimitInterface *_book;
  callback_type _callback;
  bool _is_running;
protected:
  typedef MarketMaker my_base_type;
  price_type _increment;
public:
  MarketMaker(callback_type callback=&default_callback)
    :
      _book(nullptr),
      _callback(callback),
      _is_running(false),
      _increment(0)
    {
    }
  virtual ~MarketMaker()
    {
    };
  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                     price_type incr);
  virtual void stop();

  static void default_callback(callback_msg msg,id_type id, price_type price,
                               size_type size);
  /* don't check id > 0 */
  id_type bid(price_type price, size_type size) const;
  id_type offer(price_type price, size_type size) const;
};


class MarketMaker_Simple1
    : public MarketMaker{

  typedef std::tuple<bool,price_type,size_type> order_bndl_type;
  typedef std::map<id_type,order_bndl_type> orders_map_type;
  typedef orders_map_type::value_type orders_value_type;
  size_type _sz;
  orders_map_type _my_orders;
  void _callback(callback_msg msg, id_type id, price_type price, size_type size);
  void _insert(bool buy,price_type price, size_type size);
public:

  MarketMaker_Simple1(size_type sz)
  :
    MarketMaker( std::bind(&MarketMaker_Simple1::_callback,this,_1,_2,_3,_4) ),
    _sz(sz)
  {
  }
  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
             price_type incr);
};


class MarketMaker_Random
    : public MarketMaker{
  size_type _sz_low, _sz_high;
  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr, _distr2;
  unsigned long long _gen_seed();

public:
  MarketMaker_Random(size_type sz_low, size_type sz_high,
              callback_type callback=&my_base_type::default_callback);
  MarketMaker_Random(const MarketMaker_Random& mm );
  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                  price_type incr);

private:
  static const clock_type::time_point seedtp;
};

typedef std::unique_ptr<MarketMaker> pMarketMaker;
typedef std::vector<pMarketMaker> market_makers_type;
};
#endif
