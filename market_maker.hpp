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

using namespace std::placeholders;

class MarketMaker;
typedef std::unique_ptr<MarketMaker> pMarketMaker;
typedef std::vector<pMarketMaker> market_makers_type;

market_makers_type operator+(market_makers_type&& l, market_makers_type&& r);
market_makers_type operator+(market_makers_type&& l, MarketMaker&& r);

/*
 * MarketMaker objects are intended to be 'autonomous' objects that
 * provide liquidity, receive execution callbacks, and respond with new orders.
 *
 * MarketMakers are moved into an orderbook via add_market_maker(&&)
 * or by calling add_market_makers(&&) with a market_makers_type(see below)
 *
 * The virtual start function is called by the orderbook when it is ready
 * to begin; define this to control how initial orders are inserted;
 * !! BE SURE TO CALL DOWN TO THE BASE VERSION BEFORE YOU DO ANYTHING !!
 *
 * MarketMaker::Insert<> is how you insert limit orders into the
 * SimpleOrderbook::LimitInterface passed in to the start function.
 *
 * Ideally MarketMaker should be sub-classed and a virtual _exec_callback
 * defined. But a MarketMaker(object or base class) can be instantiated
 * with a custom callback.
 *
 * To prevent the callbacks/insert mechanism from going into an unstable
 * state (e.g an infinite loop: insert->callback->insert->callback etc.) we
 * set a ::_recurse_limit that if exceeded with throw callback_overflow on
 * a call to insert<>(). It resets the count before throwing so you can catch
 * this in the callback, clean up, and exit gracefully. We also employ a larger
 * version that doesn't reset(::_tot_recurse_limit) that prevents further
 * recursion until previous callbacks come off the 'stack'
 *
 * Callbacks are handled internally by struct dynamic_functor which rebinds the
 * underlying instance when a move occurs. Callbacks are called in this order:
 *
 *   MarketMaker::_base_callback  <- handles internal admin
 *   MarketMaker::_callback_ext   <- (optionally) passed in at construction
 *   MarketMaker::_exec_callback  <- virtual, defined in subclass
 *
 *                            *   *   *
 *
 * We've restricted copy/assign and implemented move semantics to somewhat
 * protect the market makers once inside the Orderbook
 *
 * It's recommended to use the Factories and/or the + overloads to build a
 * market_makers_type that will be moved (i.e have its contents stolen) into
 * the orderbook's add_market_makers() function.
 *
 * The operator+ overloads explicity require rvalues so use std::move() if
 * necessary. They also call clear() on the market_makers_type object to make
 * it clear it's contents have been moved.
 *
 * Sub-classes of MarketMaker have to define the move_to_new(&&) virtual
 * function which steals its own contents, and passes them to a new derived
 * object inside a unique_ptr(pMarketMaker). This is intended to be used
 * internally BUT (and this not recommended) you can grab a reference -
 * to this or any object after its been moved but before it gets added to
 * the orderbook - and access it after it enters the orderbook.**
 *
 *  **This can be useful for debugging if you want to externally use the
 *    insert<bool> call to inject orders
 *
 * Its recommend to define factories in sub-classes for creating a
 * market_makers_type collection that can be moved directly into the orderbook.
 */

class MarketMakerA{
public:
  virtual ~MarketMakerA() {}
  virtual pMarketMaker move_to_new() = 0;
};

class MarketMaker
    : public MarketMakerA{

protected:
  typedef MarketMaker my_base_type;
  typedef SimpleOrderbook::LimitInterface sob_iface_type;

  struct dynamic_functor{
    friend MarketMaker;
    MarketMaker* _mm;
    callback_type _base_f, _deriv_f;

  public:
    dynamic_functor(MarketMaker* mm) : _mm(mm),c(0){
      this->rebind(mm); }
    void rebind(MarketMaker* mm)
    {
      this->_mm = mm;
      this->_base_f = std::bind(&MarketMaker::_base_callback,_mm,_1,_2,_3,_4);
      this->_deriv_f = std::bind(&MarketMaker::_exec_callback,_mm,_1,_2,_3,_4);
    }
    void operator()(callback_msg msg,id_type id,price_type price,size_type size)
    {
      ++_mm->_recurse_count;
      ++_mm->_tot_recurse_count;

      if(_mm->_tot_recurse_count <= MarketMaker::_tot_recurse_limit)
      {
        this->_base_f(msg,id,price,size);
        if(_mm->_callback_ext)
          _mm->_callback_ext(msg,id,price,size);
        this->_deriv_f(msg,id,price,size);
      }

      if(_mm->_tot_recurse_count)
        --_mm->_tot_recurse_count;
      if(_mm->_recurse_count)
        --_mm->_recurse_count;
    }
  };

  struct dynamic_functor_wrap{
    dynamic_functor* _df;
  public:
    dynamic_functor_wrap(dynamic_functor* df) : _df(df) {}
    void operator()(callback_msg msg,id_type id,price_type price,size_type size){
      _df->operator()(msg,id,price,size);
    }
  };

private:
  typedef std::unique_ptr<dynamic_functor> cb_uptr_type;
  typedef std::tuple<bool,price_type,size_type> order_bndl_type;
  typedef std::map<id_type,order_bndl_type> orders_map_type;
  typedef orders_map_type::value_type orders_value_type;

public: /*DEBUG*/
  sob_iface_type *_book;
private: /*DEBUG*/
  callback_type _callback_ext;
  cb_uptr_type _callback;
  orders_map_type _my_orders;
  bool _is_running;
  bool _last_was_buy;
  price_type _tick;
  size_type _bid_out;
  size_type _offer_out;
  long long _pos;
  int _recurse_count;
  int _tot_recurse_count;
  static const int _recurse_limit = 5;
  static const int _tot_recurse_limit = 50;

  void _base_callback(callback_msg msg,id_type id, price_type price,
                      size_type size);
  virtual void _exec_callback(callback_msg msg,id_type id, price_type price,
                               size_type size){ /* NULL */ }
  /* disable copy construction */
  MarketMaker(const MarketMaker& mm);

protected:
  inline const orders_map_type& my_orders() const { return this->_my_orders; }
  inline bool last_was_buy() const { return this->_last_was_buy; }
  inline price_type tick() const { return this->_tick; }
  inline size_type bid_out() const { return this->_bid_out; }
  inline size_type offer_out() const { return this->_offer_out; }
  inline size_type pos() const { return this->_pos; }

public:
  typedef std::initializer_list<callback_type> init_list_type;

  MarketMaker(callback_type callback = nullptr);
  MarketMaker(MarketMaker&& mm) noexcept;
  virtual ~MarketMaker() noexcept {}

  virtual pMarketMaker
  move_to_new(){ return pMarketMaker( new MarketMaker(std::move(*this)) ); }

  /* derived need to call down to start / stop */
  virtual void start(sob_iface_type *book, price_type implied, price_type tick);
  virtual void stop();

  template<bool BuyNotSell>
  void insert(price_type price, size_type size);

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(unsigned int n);
};


class MarketMaker_Simple1
    : public MarketMaker{

  size_type _sz, _max_pos;
  virtual void _exec_callback(callback_msg msg, id_type id, price_type price,
                              size_type size);
  /*
   * Do we want a copy cnstr that creates a new base with a newly bound callback?
   * Should we just keep it locked down to avoid unintended consequences ?
   */
  MarketMaker_Simple1(const MarketMaker_Simple1& mm);

public:
  typedef std::initializer_list<std::pair<size_type,size_type>> init_list_type;

  MarketMaker_Simple1(size_type sz, size_type max_pos);
  MarketMaker_Simple1(MarketMaker_Simple1&& mm) noexcept ;
  virtual ~MarketMaker_Simple1() noexcept {}

  virtual pMarketMaker move_to_new()
  {
    return pMarketMaker( new MarketMaker_Simple1(
      std::move(dynamic_cast<MarketMaker_Simple1&&>(*this))));
  }

  void start(sob_iface_type *book, price_type implied, price_type tick);

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n,size_type sz,size_type max_pos);
};


class MarketMaker_Random
    : public MarketMaker{

  size_type _max_pos;
  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr, _distr2;

  virtual void _exec_callback(callback_msg msg, id_type id, price_type price,
                              size_type size);
  unsigned long long _gen_seed();

  /*
   * Do we want a copy cnstr that creates a new base with a newly bound callback?
   * Should we just keep it locked down to avoid unintended consequences ?
   */
  MarketMaker_Random(const MarketMaker_Random& mm);

public:
  enum class dispersion{
      none = 1,
      low = 3,
      moderate = 5,
      high = 7,
      very_high = 10
    };
  typedef std::tuple<size_type,size_type,size_type,dispersion> init_params_type;
  typedef std::initializer_list<init_params_type> init_list_type;



  MarketMaker_Random(size_type sz_low, size_type sz_high, size_type max_pos,
                     dispersion d = dispersion::moderate);
  MarketMaker_Random(MarketMaker_Random&& mm) noexcept;
  virtual ~MarketMaker_Random() noexcept {}

  virtual pMarketMaker move_to_new()
  {
    return pMarketMaker( new MarketMaker_Random(
      std::move(dynamic_cast<MarketMaker_Random&&>(*this))));
  }

  void start(sob_iface_type *book, price_type implied, price_type tick);

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n, size_type sz_low,
                                    size_type sz_high, size_type max_pos,
                                    dispersion d);

private:
  static const clock_type::time_point seedtp;
};



};
#endif
