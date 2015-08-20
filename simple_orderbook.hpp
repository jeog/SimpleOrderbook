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

#ifndef JO_0815_SIMPLE_ORDERBOOK
#define JO_0815_SIMPLE_ORDERBOOK

#include <map>
#include <vector>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <string>
#include <random>
#include <functional>
#include <tuple>
#include <queue>
#include <stack>
#include <chrono>
#include <ctime>
#include <string>
#include <ratio>

namespace NativeLayer{

//template< typename IncrementRatio = std::ratio<1,100>>
//class SimpleOrderbook;

typedef float               price_type;
typedef double              price_diff_type;
typedef unsigned long       size_type, id_type;
typedef long long           size_diff_type;
typedef unsigned long long  large_size_type, safe_uint_type;

typedef std::pair<price_type,size_type>         limit_order_type;
typedef std::pair<price_type,limit_order_type>  stop_order_type;

typedef typename std::chrono::steady_clock      clock_type;

typedef std::function<void(id_type,price_type,size_type)> fill_callback_type;

std::ostream& operator<<(std::ostream& out, limit_order_type lim);
std::ostream& operator<<(std::ostream& out, stop_order_type stp);

class liquidity_exception : std::runtime_error{
public:
  liquidity_exception(const char* what)
    :
    std::runtime_error(what)
    {
    }
};

class invalid_order : std::invalid_argument{
public:
  invalid_order(const char* what)
    :
     std::invalid_argument(what)
    {
    }
};


class MarketMaker{
 /*
  * an object that provides customizable orderflow from asymmetric market info
  *
  * currently just returns limit orders from a random range when
  * SimpleOrderbook calls on post_bid/post_ask
  */
  size_type _sz_low, _sz_high;

  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr;

  static const clock_type::time_point seedtp;

public:
  MarketMaker( size_type sz_low, size_type sz_high );
  MarketMaker( const MarketMaker& mm );
  limit_order_type post_bid(price_type price);
  limit_order_type post_ask(price_type price);

  static void default_callback(id_type id, price_type price, size_type size);
};

template< typename StartRatio = std::ratio<50,1>,
          typename IncrementRatio = std::ratio<1,100>  >
class SimpleOrderbook{
 /*
  * TODO add price and size limits, max increment precision
  * TODO add 'this' consts where appropriate
  * TODO decide on consistent bid/ask, buy/sell, offer syntax
  * TODO cache high/low limit orders in book to avoid checking entire thing
  * TODO adjust cached high/low stop/limit orders on execution AND pull/remove
  * TOOD review how we copy/move/PY_INCREF callbacks
  * TODO tighten up timestamp/t&s
  * TODO use ratio to handle floats
  */
public:
  typedef typename clock_type::time_point                   time_stamp_type;
  typedef std::tuple<time_stamp_type,price_type,size_type>  t_and_s_type;
  typedef std::vector< t_and_s_type >                       time_and_sales_type;
  typedef std::map<price_type,size_type>                    market_depth_type;

private:

  typedef std::ratio<1,100>  minimum_ratio;
  typedef std::ratio<1,1> one;

  typedef std::ratio_subtract< std::ratio_divide<StartRatio,IncrementRatio>,
                      one> lower_increments_ratio;

  typedef std::ratio_subtract<StartRatio,
                             std::ratio_multiply<lower_increments_ratio,
                                                IncrementRatio>>  base_ratio;

  static constexpr size_type lower_increments =
    floor(lower_increments_ratio::num / lower_increments_ratio::den);

  static constexpr size_type total_increments = lower_increments * 2 + 1;
  static constexpr size_type minimum_increments = 100;

  static_assert( !std::ratio_less<IncrementRatio,minimum_ratio>::value,
                 "IncrementRatio to can not be less than 1/1000 " );
  static_assert(total_increments >= minimum_increments, "not enough increments");

  const price_type incr = (price_type)IncrementRatio::num / IncrementRatio::den;
  const price_type base = (price_type)base_ratio::num / (base_ratio::den);
  const price_type rounder = std::max<price_type>((price_type)1/incr, 1);

  /*
   * limit bundle type: holds the size and callback of each limit order
   * limit 'chain' type: holds all limit orders at a price
   */
  typedef std::pair<size_type, fill_callback_type>         limit_bndl_type;
  typedef std::map<id_type, limit_bndl_type>               limit_chain_type;
  /*
   * stop bundle type: holds the size and callback of each stop order
   * stop 'chain' type: holds all stop orders at a price(stop-limit/stop-market)
   */
  typedef std::pair<limit_order_type, fill_callback_type>  stop_bndl_type;
  typedef std::map<id_type, stop_bndl_type>                stop_chain_type;
  /*
   * chain pair is the limit and stop chain at a particular price
   */

public: /* DEBUG */
  typedef std::pair<limit_chain_type,stop_chain_type>  *plevel, chain_pair_type ;
private:
  /*
   * chain array is an array of all chain pairs
   */
  typedef std::unique_ptr<chain_pair_type[],
                          void(*)(chain_pair_type*)>      order_book_type;

  /*
   * how callback info is stored in the deferred callback queue
   */
  typedef std::tuple<fill_callback_type,fill_callback_type,
                     id_type, id_type, price_type,size_type>  dfrd_cb_elem_type;

  /* state fields */
  size_type _bid_size, _ask_size, _last_size;

  /* THE ORDER BOOK */
  order_book_type _book;

  plevel _min_plevel, _max_plevel, _last_plevel, _bid_plevel, _ask_plevel,
         _low_buy_limit_plevel, _high_sell_limit_plevel,
         _low_buy_stop_plevel, _high_sell_stop_plevel;

  large_size_type _total_volume, _last_id;


  /* autonomous market makers */
  std::vector<MarketMaker > _market_makers;

  /* trade has occurred but we've deferred 'handling' it */
  bool _is_dirty;

  /* store deferred callback info until we are clear to execute */
  std::queue<dfrd_cb_elem_type> _deferred_callback_queue;

  /*
   * time & sales
   */
  std::vector< t_and_s_type > _t_and_s;
  size_type _t_and_s_max_sz;
  bool _t_and_s_full;

  /*
   * user input checks
   *//*
  inline bool _check_order_size(size_type sz){ return sz > 0; }
  inline bool _check_order_price(price_type price)
  {
    return price > 0 && price <= this->_max_plevel;
  }

  /* don't worry about overflow */
  inline large_size_type _generate_id(){ return ++(this->_last_id); }

  inline price_type _align(price_type price)
  { /* attempt to deal with internal floating point issues */
    return this->_ptrtop(this->_ptoptr(price));
  }

  /*
   * price-to-index and index-to-price utilities
   */
public:
  chain_pair_type* _ptoptr(price_type price);
  price_type _ptrtop(chain_pair_type* plev);
private:

/*
  template< typename ChainArrayTy>
  market_depth_type _market_depth(const ChainArrayTy& array)
  { /*
     * calculate chain_size at each price level
     *//*
    ASSERT_VALID_CHAIN_ARRAY(ChainArrayTy);
    return market_depth_type();
  }

  template< typename ChainArrayTy>
  size_type _chain_size(const ChainArrayTy& array, price_type price)
  { /*
     * calculate total volume in the chain
     *//*
    ASSERT_VALID_CHAIN_ARRAY(ChainArrayTy);

    size_type sz = 0;
    for( typename ChainArrayTy::element_type::value_type& e
         : *(this->_find_order_chain(array,price))){
      sz += e.second.first;
    }
    return sz;
  }

  template< typename ChainArrayTy >
  void _dump_chain_array(ChainArrayTy& ca)
  { /*
     *dump (to stdout) a particular chain array
     *//*
    ASSERT_VALID_CHAIN_ARRAY(ChainArrayTy);

    size_type sz;
    typename ChainArrayTy::element_type* porders;

    for(long long i = this->_full_range - 1; i >= 0; --i){
      porders = &(ca[i]);
      sz = porders->size();
      if(sz)
        std::cout<< this->_ptrtop(i);
      for(typename ChainArrayTy::element_type::value_type& elem : *porders)
        std::cout<< " <" << elem.second.first << "> ";
      if(sz)
        std::cout<< std::endl;
    }
  }

  template< typename ChainArrayTy >
  bool _remove_order_from_chain_array(ChainArrayTy& ca, id_type id)
  { /*
     * remove order from particular chain array, return success boolean
     *//*
    ASSERT_VALID_CHAIN_ARRAY(ChainArrayTy);

    typename ChainArrayTy::element_type* porders;

    for(long long i = this->_full_range - 1; i >= 0; --i){
      porders = &(ca[i]);
      for(typename ChainArrayTy::element_type::value_type& elem : *porders){
        if(elem.first == id){
          porders->erase(id);
          return true;
        }
      }
    }
    return false;
  }

  template<typename ChainArrayTy>
  inline typename ChainArrayTy::element_type*
  _find_order_chain(const ChainArrayTy& array, price_type price)
  { /*
     * get chain ptr by price
     *//*
    ASSERT_VALID_CHAIN_ARRAY(ChainArrayTy);

    return &(array[this->_ptoptr(price)]);
  }

  /*
   * handle post-trade tasks
   */
  void _on_trade_completion();
  void _look_for_triggered_stops();
  void _handle_triggered_stop_chain(price_type price,bool ask_side);

  /*
   * execute if orders match
   */
  size_type _lift_offers(price_type price, id_type id, size_type size,
                         fill_callback_type& callback);
  size_type _hit_bids(price_type price, id_type id, size_type size,
                      fill_callback_type& callback);
  /*
   * signal trade has occurred(admin only, DONT INSERT NEW TRADES FROM HERE!)
   */
  void _trade_has_occured(price_type price, size_type size, id_type id_buyer,
                          id_type id_seller, fill_callback_type& cb_buyer,
                          fill_callback_type& cb_seller, bool took_offer);
  /*
   * internal insert orders once/if we have an id
   */
  void _insert_limit_order(bool buy, price_type limit, size_type size,
                            fill_callback_type callback, id_type id);

  void _insert_market_order(bool buy, size_type size,
                             fill_callback_type callback, id_type id);

  void _insert_stop_order(bool buy, price_type stop, size_type size,
                           fill_callback_type callback, id_type id);

  void _insert_stop_order(bool buy, price_type stop, price_type limit,
                           size_type size, fill_callback_type callback,
                           id_type id);
  /*
   * initialize book and market makers; CAREFUL: called from constructor
   */
  void _init(size_type begin_from_init, size_type end_from_init);

  /***************************************************
   *** RESTRICT COPY / MOVE / ASSIGN ... (for now) ***
   **************************************************/
  SimpleOrderbook(const SimpleOrderbook& sob);
  SimpleOrderbook(SimpleOrderbook&& sob);
  SimpleOrderbook& operator==(const SimpleOrderbook& sob);
  /***************************************************
   *** RESTRICT COPY / MOVE / ASSIGN ... (for now) ***
   **************************************************/

public:
  SimpleOrderbook(std::vector<MarketMaker>& mms);

  ~SimpleOrderbook();

  /*
   * PUBLICE INTERFACE FOR INSERTING, PULLING, AND REPLACING ORDERS
   */
  id_type insert_limit_order(bool buy, price_type limit, size_type size,
                             fill_callback_type callback);
  id_type insert_market_order(bool buy, size_type size,
                              fill_callback_type callback);
  id_type insert_stop_order(bool buy, price_type stop, size_type size,
                            fill_callback_type callback);
  id_type insert_stop_order(bool buy, price_type stop, price_type limit,
                            size_type size, fill_callback_type callback);

  bool pull_order(id_type id);

  id_type replace_with_limit_order(id_type id, bool buy, price_type limit,
                                   size_type size, fill_callback_type callback);
  id_type replace_with_market_order(id_type id, bool buy, size_type size,
                                    fill_callback_type callback);
  id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                  size_type size, fill_callback_type callback);
  id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                  price_type limit, size_type size,
                                  fill_callback_type callback);
  /*
   * PUBLIC INTERFACE FOR DUMPING A 'CHAIN_ARRAY' (part of the orderbook)
   *//*
  inline void dump_buy_limits(){ this->_dump_chain_array(this->_bid_limits); }
  inline void dump_sell_limits(){ this->_dump_chain_array(this->_ask_limits); }
  inline void dump_buy_stops(){ this->_dump_chain_array(this->_bid_stops); }
  inline void dump_sell_stops(){ this->_dump_chain_array(this->_ask_stops); }

  /*
   * PUBLIC INTERFACE FOR RETRIEVING BASIC MARKET INFO
   *//*
  inline price_type bid_price(){ return this->_align(this->_bid_plevel); }
  inline price_type ask_price(){ return this->_align(this->_ask_plevel); }
  inline price_type last_price(){ return this->_align(this->_last_plevel); }
  inline size_type bid_size(){ return this->_bid_size; }
  inline size_type ask_size(){ return this->_ask_size; }
  inline size_type last_size(){ return this->_last_size; }
  inline large_size_type volume(){ return this->_total_volume; }

  /* PUBLIC INTERFACE FOR RETRIEVING THE VECTOR OF TIME & SALES DATA */
  inline const time_and_sales_type& time_and_sales()
  {
    return this->_t_and_s;
  }

  /* convert time & sales chrono timepoint to str via ctime */
  static std::string timestamp_to_str(const time_stamp_type& tp);
};

template<typename T1>
inline std::string cat(T1 arg1){ return std::string(arg1); }
template<typename T1, typename... Ts>
inline std::string cat(T1 arg1, Ts... args)
{
  return std::string(arg1) + cat(args...);
}

};

#include "simple_orderbook.tpp"

#endif
