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

#include <random>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <ratio>
#include <algorithm>

#include "../interfaces.hpp"
#include "../types.hpp"

namespace NativeLayer{

/* move forward declrs to types.hpp */

using namespace std::placeholders;

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
 * to begin; define this (as protected, orderbook is a friend class) to control
 * how initial orders are inserted;
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
 * Sub-classes of MarketMaker have to define the _move_to_new(&&) private virtual
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
  virtual pMarketMaker _move_to_new() = 0;
public:
  virtual ~MarketMakerA() {}
};

class MarketMaker
    : public MarketMakerA{

  friend SimpleOrderbook::QuarterTick;
  friend SimpleOrderbook::TenthTick;
  friend SimpleOrderbook::ThirtySecondthTick;
  friend SimpleOrderbook::HundredthTick;
  friend SimpleOrderbook::ThousandthTick;
  friend SimpleOrderbook::TenThousandthTick;
  friend market_makers_type operator+(market_makers_type&& l,
                                      market_makers_type&& r);
  friend market_makers_type operator+(market_makers_type&& l, MarketMaker&& r);

public:
  typedef SimpleOrderbook::LimitInterface sob_iface_type;

protected:
  typedef MarketMaker my_base_type;

private:
  struct dynamic_functor{
    MarketMaker* _mm;
    bool _mm_alive;
    order_exec_cb_type _base_f, _deriv_f;
  public:
    dynamic_functor(MarketMaker* mm)
        : _mm(mm), _mm_alive(true) {this->rebind(mm);}
    void rebind(MarketMaker* mm)
    {
      this->_mm = mm;
      this->_base_f = std::bind(&MarketMaker::_base_callback,_mm,_1,_2,_3,_4);
      this->_deriv_f = std::bind(&MarketMaker::_exec_callback,_mm,_1,_2,_3,_4);
    }
    void operator()(callback_msg msg,id_type id,price_type price,size_type size)
    {
      if(!this->_mm_alive)
        return;
      ++_mm->_recurse_count;
      ++_mm->_tot_recurse_count;
      if(_mm->_tot_recurse_count <= MarketMaker::_tot_recurse_limit)
      {
        this->_base_f(msg,id,price,size);
        if(_mm->_callback_ext)
          _mm->_callback_ext(msg,id,price,size);
        this->_deriv_f(msg,id,price,size);
      }
      if(_mm->_tot_recurse_count) --_mm->_tot_recurse_count;
      if(_mm->_recurse_count)     --_mm->_recurse_count;
    }
    inline void kill() { this->_mm_alive = false; }
  };

  typedef std::shared_ptr<dynamic_functor> df_sptr_type;
public:
  struct dynamic_functor_wrap{
    df_sptr_type _df;
  public:
    dynamic_functor_wrap(df_sptr_type df = nullptr) : _df(df) {}
    void operator()(callback_msg msg,id_type id,price_type price,size_type size)
    {
      if(_df) _df->operator()(msg,id,price,size);
    }
    operator bool(){ return (bool)_df; }
    inline void kill() { if(_df) _df->kill(); }
  };
private:

  typedef std::tuple<bool,price_type,size_type> order_bndl_type;
  typedef std::map<id_type,order_bndl_type> orders_map_type;
  typedef orders_map_type::value_type orders_value_type;

  typedef struct{
    bool is_buy;
    price_type price;
    size_type size;
  }fill_info;

  sob_iface_type *_book;
  order_exec_cb_type _callback_ext;
  df_sptr_type _callback;
  orders_map_type _my_orders;
  bool _is_running;
  fill_info _this_fill, _last_fill;
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

  virtual pMarketMaker _move_to_new()
  {
    return pMarketMaker( new MarketMaker(std::move(*this)) );
  }

  /* disable copy construction */
  MarketMaker(const MarketMaker& mm);

protected:
  inline const orders_map_type& my_orders() const { return this->_my_orders; }
  inline bool this_fill_was_buy() const { return this->_this_fill.is_buy; }
  inline bool last_fill_was_buy() const { return this->_last_fill.is_buy; }
  inline price_type this_fill_price() const { return this->_this_fill.price; }
  inline price_type last_fill_price() const { return this->_last_fill.price; }
  inline size_type this_fill_size() const { return this->_this_fill.size; }
  inline size_type last_fill_size() const { return this->_last_fill.size; }
  inline size_type tick_chng() const
  {
    return tick_diff(this->_this_fill.price,this->_last_fill.price,this->tick());
  }
  inline price_type tick() const { return this->_tick; }
  inline size_type bid_out() const { return this->_bid_out; }
  inline size_type offer_out() const { return this->_offer_out; }
  inline size_type pos() const { return this->_pos; }

  template<bool BuyNotSell>
  size_type random_remove(price_type minp, id_type this_id);

  /* derived need to call down to start / stop */
  virtual void start(sob_iface_type *book, price_type implied, price_type tick);
  virtual void stop();
  template<bool BuyNotSell>
  void insert(price_type price, size_type size, bool no_order_cb = false);

public:
  typedef std::initializer_list<order_exec_cb_type> init_list_type;
  MarketMaker(order_exec_cb_type callback = nullptr);
  MarketMaker(MarketMaker&& mm) noexcept;

  virtual ~MarketMaker() noexcept
    { /* if alive change state of callback so we don't used freed memory */
      if(this->_callback) this->_callback->kill();
    }

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(unsigned int n);
  inline static size_type tick_diff(price_type p1, price_type p2, price_type t)
  {
    return (p1 - p2) / t;
  }
};

template<bool BuyNotSell>
void MarketMaker::insert(price_type price, size_type size, bool no_order_cb)
{

  if(!this->_is_running)
    throw invalid_state("market/market-maker is not in a running state");

  if(this->_recurse_count > this->_recurse_limit){
    /*
     * note we are reseting the count; caller can catch and keep going with
     * the recursive calls if they want, we did our part...
     */
    this->_recurse_count = 0;
    throw callback_overflow("market maker trying to insert after exceeding the"
                            " recursion limit set for the callback stack");
  }


  this->_book->insert_limit_order(BuyNotSell, price,size,
                                  (!no_order_cb
                                     ? dynamic_functor_wrap(this->_callback)
                                     : dynamic_functor_wrap(nullptr) )                                   ,
    /*
     * the post-insertion / pre-completion callback
     *
     * this guarantees to complete before the standard callbacks for
     * this order can be triggered
     */
    [=](id_type id)
    {
      if(id == 0)
        throw invalid_order("order could not be inserted");

      this->_my_orders.insert(orders_value_type(id,order_bndl_type(BuyNotSell,
                                                                   price,size)));

      if(BuyNotSell) this->_bid_out += size;
      else           this->_offer_out += size;
    });
}

template<bool BuyNotSell>
size_type MarketMaker::random_remove(price_type minp, id_type this_id)
{ /*
   * O(n) as-is
   * could be O(c) in pracice if we exclude minp:
   *   (1 - .50^n) chance we find it in n iters
   */
  size_type s;
  orders_map_type::const_iterator riter, eiter;

  eiter = this->_my_orders.end();
  riter = std::find_if(this->_my_orders.cbegin(),eiter,
                      [=](orders_value_type p){
                        return std::get<0>(p.second) == BuyNotSell
                            && (BuyNotSell ? std::get<1>(p.second) < minp
                                           : std::get<1>(p.second) > minp)
                            && p.first != this_id ;
                     });
  s = 0;
  if(riter != eiter){
    s = std::get<2>(riter->second);
   //std::cout<< "pulling: " << std::to_string(riter->first) << std::endl;
    this->_book->pull_order(riter->first);
  }
  return s;
}


class MarketMaker_Simple1
    : public MarketMaker{

  size_type _sz, _max_pos;
  virtual void _exec_callback(callback_msg msg, id_type id, price_type price,
                              size_type size);

  virtual pMarketMaker _move_to_new()
  {
    return pMarketMaker( new MarketMaker_Simple1(
      std::move(dynamic_cast<MarketMaker_Simple1&&>(*this))));
  }

  /* disable copy construction */
  MarketMaker_Simple1(const MarketMaker_Simple1& mm);

protected:
  void start(sob_iface_type *book, price_type implied, price_type tick);

public:
  typedef std::initializer_list<std::pair<size_type,size_type>> init_list_type;

  MarketMaker_Simple1(size_type sz, size_type max_pos);
  MarketMaker_Simple1(MarketMaker_Simple1&& mm) noexcept ;
  virtual ~MarketMaker_Simple1() noexcept {}

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n,size_type sz,size_type max_pos);
};


class MarketMaker_Random
    : public MarketMaker{

public:
  enum class dispersion{
      none = 1,
      low = 3,
      moderate = 5,
      high = 7,
      very_high = 10
    };

private:
  size_type _max_pos, _lowsz, _highsz, _midsz, _bcumm, _scumm;
  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr, _distr2;
  dispersion _disp;

  virtual void _exec_callback(callback_msg msg, id_type id, price_type price,
                              size_type size);
  unsigned long long _gen_seed();

  virtual pMarketMaker _move_to_new()
  {
    return pMarketMaker( new MarketMaker_Random(
      std::move(dynamic_cast<MarketMaker_Random&&>(*this))));
  }

  /* disable copy construction */
  MarketMaker_Random(const MarketMaker_Random& mm);

protected:
  void start(sob_iface_type *book, price_type implied, price_type tick);

public:
  typedef std::tuple<size_type,size_type,size_type,dispersion> init_params_type;
  typedef std::initializer_list<init_params_type> init_list_type;

  MarketMaker_Random(size_type sz_low, size_type sz_high, size_type max_pos,
                     dispersion d = dispersion::moderate);
  MarketMaker_Random(MarketMaker_Random&& mm) noexcept;
  virtual ~MarketMaker_Random() noexcept {}

  static market_makers_type Factory(init_list_type il);
  static market_makers_type Factory(size_type n, size_type sz_low,
                                    size_type sz_high, size_type max_pos,
                                    dispersion d);
private:
  static const clock_type::time_point seedtp;
};



};
#endif
