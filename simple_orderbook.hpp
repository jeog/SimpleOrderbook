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

namespace NativeLayer{

class SimpleOrderbook;

typedef float               price_type;
typedef double              price_diff_type;
typedef unsigned long       size_type, id_type;
typedef long long           size_diff_type;
typedef unsigned long long  large_size_type;

typedef std::pair<price_type,size_type>         limit_order_type;
typedef std::pair<price_type,limit_order_type>  stop_order_type;

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
  const SimpleOrderbook& _my_vob;

public:
  MarketMaker( size_type sz_low, size_type sz_high, const SimpleOrderbook& vob);
  MarketMaker( const MarketMaker& mm );
  limit_order_type post_bid(price_type price);
  limit_order_type post_ask(price_type price);
  static void default_callback(id_type id, price_type price, size_type size);
};


class SimpleOrderbook{
 /*
  * TODO add price and size limits, max increment precision
  * TODO add 'this' consts where appropriate
  * TODO decide on consistent bid/ask, buy/sell, offer syntax
  * TODO cache high/low orders in book to avoid checking entire thing
  * TOOD review how we copy/move/PY_INCREF callbacks
  * TODO tighten up timestamp/t&s
  */
public:
  typedef typename std::chrono::steady_clock                clock_type;
  typedef typename clock_type::time_point                   time_stamp_type;
  typedef std::tuple<time_stamp_type,price_type,size_type>  t_and_s_type;
  typedef std::vector< t_and_s_type >                       time_and_sales_type;

private:
  /*
   * limit bundle type: holds the size and callback of each limit order
   * limit 'chain' type: holds all limit orders at a price
   * limit array type: holds all the limit order chains
   */
  typedef std::pair<size_type, fill_callback_type>     limit_bndl_type;
  typedef std::map<id_type, limit_bndl_type>           limit_chain_type;
  typedef std::unique_ptr<limit_chain_type[],
                          void(*)(limit_chain_type*)>  limit_array_type;
  /*
   * stop bundle type: holds the size and callback of each stop order
   * stop 'chain' type: holds all stop orders at a price(stop-limit/stop-market)
   *   if price_type in stop_bndl_type is <=0 its a market order
   * stop array type: holds all the limit order chains
   */
  typedef std::pair<limit_order_type, fill_callback_type>  stop_bndl_type;
  typedef std::map<id_type, stop_bndl_type>                stop_chain_type;
  typedef std::unique_ptr<stop_chain_type[],
                          void(*)(stop_chain_type*)>       stop_array_type;

  /*
   * how callback info is stored in the deferred callback queue
   */
  typedef std::tuple<fill_callback_type,fill_callback_type,
                     id_type, id_type, price_type,size_type>  dfrd_cb_elem_type;

  /*
   * state fields
   */
  price_type _incr, _incr_err, _init_price, _last_price,
             _bid_price, _ask_price, _min_price, _max_price,
             _low_bid_stop, _high_ask_stop;
  size_type _bid_size, _ask_size, _mm_sz_high, _mm_sz_low,
           _lower_range, _full_range, _last_size;
  large_size_type _total_volume, _last_id;

  /*
   * order arrays
   */
  limit_array_type _bid_limits, _ask_limits;
  stop_array_type _bid_stops, _ask_stops;

  /* autonomous market makers */
  std::vector<MarketMaker> _market_makers;

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
   */
  inline bool _check_order_size(size_type sz){ return sz > 0; }
  inline bool _check_order_price(price_type price)
  {
    return price > 0 && price <= this->_max_price;
  }

  inline large_size_type _generate_id()
  { /* don't worry about overflow */
    return ++(this->_last_id);
  }

  inline price_type _align(price_type price)
  { /* attempt to deal with internal floating point issues */
    return this->_itop(this->_ptoi(price));
  }

  /*
   * price-to-index and index-to-price utilities
   */
  size_type _ptoi(price_type price);
  price_type _itop(size_type index);

  template<typename ChainArrayTy>
  struct _array_type_check_{
    static_assert( std::is_same<ChainArrayTy,limit_array_type>::value ||
                   std::is_same<ChainArrayTy,stop_array_type>::value,
                   "type not limit_array_type or stop_array_type");
  };

  template< typename ChainArrayTy>
  size_type _chain_size(const ChainArrayTy& array, price_type price)
  { /*
     * calculate total volume in the chain
     */
    _array_type_check_<ChainArrayTy>();
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
     */
    _array_type_check_<ChainArrayTy>();
    size_type sz;
    typename ChainArrayTy::element_type* porders;

    for(long long i = this->_full_range - 1; i >= 0; --i){
      porders = &(ca[i]);
      sz = porders->size();
      if(sz)
        std::cout<< this->_itop(i);
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
     */
    _array_type_check_<ChainArrayTy>();
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
  { /* get chain ptr by price */
    _array_type_check_<ChainArrayTy>();
    return &(array[this->_ptoi(price)]);
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
  SimpleOrderbook(const SimpleOrderbook& vob);
  SimpleOrderbook(SimpleOrderbook&& vob);
  SimpleOrderbook& operator==(const SimpleOrderbook& vob);
  /***************************************************
   *** RESTRICT COPY / MOVE / ASSIGN ... (for now) ***
   **************************************************/

public:
  SimpleOrderbook(price_type price, price_type incr, size_type mm_num,
                   size_type mm_sz_low, size_type mm_sz_high,
                   size_type mm_init_levels);

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
   */
  inline void dump_buy_limits(){ this->_dump_chain_array(this->_bid_limits); }
  inline void dump_sell_limits(){ this->_dump_chain_array(this->_ask_limits); }
  inline void dump_buy_stops(){ this->_dump_chain_array(this->_bid_stops); }
  inline void dump_sell_stops(){ this->_dump_chain_array(this->_ask_stops); }

  /*
   * PUBLIC INTERFACE FOR RETRIEVING BASIC MARKET INFO
   */
  inline price_type bid_price(){ return this->_align(this->_bid_price); }
  inline price_type ask_price(){ return this->_align(this->_ask_price); }
  inline price_type last_price(){ return this->_align(this->_last_price); }
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

#endif
