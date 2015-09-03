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
#include <memory>
#include <map>
#include <mutex>


namespace NativeLayer{

namespace SimpleOrderbook{
class LimitInterface;
}

class MarketMaker;
typedef std::unique_ptr<MarketMaker> pMarketMaker;
typedef std::vector<pMarketMaker> market_makers_type;

market_makers_type operator+(market_makers_type& l, market_makers_type& r);

class MarketMaker{
protected:
  typedef MarketMaker my_base_type;
  typedef std::tuple<bool,price_type,size_type> order_bndl_type;
  typedef std::map<id_type,order_bndl_type> orders_map_type;
  typedef orders_map_type::value_type orders_value_type;

private:
  SimpleOrderbook::LimitInterface *_book;
  callback_type _exec_callback_obj_x;
  callback_type _exec_callback_obj;
  orders_map_type _my_orders;
  bool _is_running;
  bool _last_was_buy;
  price_type _tick;
  size_type _bid_out;
  size_type _offer_out;
  long long _pos;

  void _base_callback(callback_msg msg,id_type id, price_type price,
                      size_type size);

  /*
   * disable copy construction:
   * derived should call down for new base with the callback for that instance
   */
  MarketMaker(const MarketMaker& mm);

protected:
  inline const orders_map_type& my_orders() const { return this->_my_orders; }
  inline bool last_was_buy() const { return this->_last_was_buy; }
  inline price_type tick() const { return this->_tick; }
  inline size_type bid_out() const { return this->_bid_out; }
  inline size_type offer_out() const { return this->_offer_out; }
  inline size_type pos() const { return this->_pos; }

  virtual void _exec_callback(callback_msg msg,id_type id, price_type price,
                              size_type size)
  {
  }

  struct callback_functor{
    MarketMaker* _mm;
  public:
    callback_functor(MarketMaker* mm) : _mm(mm) {}
    virtual ~callback_functor();
    virtual void rebind(MarketMaker* mm) { _mm = mm; }
    virtual void operator()()
    {


    }
  };

public:
  MarketMaker(callback_type exec_callback = nullptr);
 // MarketMaker(MarketMaker&& mm);

  virtual ~MarketMaker()
    {
    };

  /* derived need to call down to start / stop */
  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                     price_type tick);
  virtual void stop();

  template<bool BuyNotSell>
  void insert(price_type price, size_type size);

  static market_makers_type Factory(std::initializer_list<callback_type> il);
  static market_makers_type Factory(unsigned int n);
};


class MarketMaker_Simple1
    : public MarketMaker{

  size_type _sz, _max_pos;
  void _exec_callback(callback_msg msg, id_type id, price_type price,
                      size_type size);
  /*
   * Do we want a copy cnstr that creates a new base with a newly bound callback?
   * Should we just keep it locked down to avoid unintended consequences ?
   */
  MarketMaker_Simple1(const MarketMaker_Simple1& mm);

public:
  typedef std::initializer_list<std::pair<size_type,size_type>> init_list_type;

  MarketMaker_Simple1(size_type sz, size_type max_pos);
// MarketMaker_Simple1(MarketMaker_Simple1&& mm);

  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                     price_type tick);

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n,size_type sz,size_type max_pos);
};


class MarketMaker_Random
    : public MarketMaker{

  size_type _max_pos;
  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr, _distr2;

  void _exec_callback(callback_msg msg, id_type id, price_type price,
                      size_type size);
  unsigned long long _gen_seed();

  /*
   * Do we want a copy cnstr that creates a new base with a newly bound callback?
   * Should we just keep it locked down to avoid unintended consequences ?
   */
  MarketMaker_Random(const MarketMaker_Random& mm);

public:
  typedef std::tuple<size_type,size_type,size_type> size_params_type;
  typedef std::initializer_list<size_params_type> init_list_type;
  enum class dispersion{
    none = 1,
    low = 3,
    moderate = 5,
    high = 7,
    very_high = 10,
  };

  MarketMaker_Random(size_type sz_low, size_type sz_high, size_type max_pos,
                     dispersion d = dispersion::moderate);
 // MarketMaker_Random(MarketMaker_Random&& mm);

  virtual void start(SimpleOrderbook::LimitInterface *book, price_type implied,
                     price_type tick);

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n, size_type sz_low,
                                    size_type sz_high, size_type max_pos);

private:
  static const clock_type::time_point seedtp;
};



};
#endif
