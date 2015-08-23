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
#include <array>
#include <exception>

namespace NativeLayer{

typedef float               price_type;
typedef double              price_diff_type;
typedef unsigned long       size_type, id_type;
typedef long long           size_diff_type;
typedef unsigned long long  large_size_type;

typedef std::pair<price_type,size_type>         limit_order_type;
typedef std::pair<price_type,limit_order_type>  stop_order_type;

typedef typename std::chrono::steady_clock      clock_type;

typedef const enum {
    cancel = 0,
    fill
}callback_msg;

typedef std::function<void(callback_msg,id_type,
                           price_type,size_type)> fill_callback_type;

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

class cache_value_error : std::runtime_error{
public:
  cache_value_error(const char* what)
    :
     std::runtime_error(what)
    {
    }
};

template<typename T1>
inline std::string cat(T1 arg1){ return std::string(arg1); }
template<typename T1, typename... Ts>
inline std::string cat(T1 arg1, Ts... args)
{
  return std::string(arg1) + cat(args...);
}

namespace SimpleOrderbook{

class QueryInterface{
protected:
  QueryInterface()
    {
    }
  virtual ~QueryInterface()
    {
    }
public:
  /* where in the hierachy these go depends on what we give to whom */
  typedef typename clock_type::time_point                   time_stamp_type;
  typedef std::tuple<time_stamp_type,price_type,size_type>  t_and_s_type;
  typedef std::vector< t_and_s_type >                       time_and_sales_type;
  typedef std::map<price_type,size_type>                    market_depth_type;

  virtual price_type bid_price() const = 0;
  virtual price_type ask_price() const = 0;
  virtual price_type last_price() const = 0;
  virtual size_type bid_size() const = 0;
  virtual size_type ask_size() const = 0;
  virtual size_type last_size() const = 0;
  virtual large_size_type volume() const = 0;
  virtual large_size_type last_id() const = 0;
  virtual market_depth_type bid_depth(size_type depth=8) const = 0;
  virtual market_depth_type ask_depth(size_type depth=8) const = 0;
  virtual const time_and_sales_type& time_and_sales() const = 0;
};

class LimitInterface
    : protected QueryInterface{
protected:
  LimitInterface()
    {
    }
  virtual ~LimitInterface()
    {
    }
public:
  virtual id_type insert_limit_order(bool buy, price_type limit, size_type size,
                                    fill_callback_type callback) = 0;
  virtual id_type replace_with_limit_order(id_type id, bool buy,
                                           price_type limit,
                                           size_type size,
                                           fill_callback_type callback) = 0;
  virtual bool pull_order(id_type id, bool search_limits_first=true) = 0;
};

class Interface
    : protected LimitInterface{
protected:
  Interface()
    {
    }
  virtual ~Interface()
    {
    }
public:
  virtual id_type insert_market_order(bool buy, size_type size,
                                    fill_callback_type callback) = 0;
  virtual id_type insert_stop_order(bool buy, price_type stop, size_type size,
                                    fill_callback_type callback) = 0;
  virtual id_type insert_stop_order(bool buy, price_type stop, price_type limit,
                                    size_type size,
                                    fill_callback_type callback) = 0;
  virtual id_type replace_with_market_order(id_type id, bool buy,
                                            size_type size,
                                            fill_callback_type callback) = 0;
  virtual id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                          size_type size,
                                          fill_callback_type callback) = 0;
  virtual id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                          price_type limit, size_type size,
                                          fill_callback_type callback) = 0;
};

};

class MarketMaker{
 /*
  * an object that provides customizable orderflow from asymmetric market info
  *
  * currently just returns limit orders from a random range when
  * SimpleOrderbook calls on post_bid/post_ask
  */
  size_type _sz_low, _sz_high;
  SimpleOrderbook::LimitInterface *_book;
  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<size_type> _distr;
  std::uniform_int_distribution<size_type> _distr2;

public:
  MarketMaker( size_type sz_low, size_type sz_high );
  MarketMaker( const MarketMaker& mm );
  void initialize(SimpleOrderbook::LimitInterface *book,
                  price_type implied, price_type incr);
  limit_order_type post_bid(price_type price);
  limit_order_type post_ask(price_type price);

  static void default_callback(callback_msg msg,id_type id, price_type price,
                               size_type size);
private:
  static const clock_type::time_point seedtp;
};


#define SOB_TEMPLATE template< size_type StartPrice, size_type DecimalPlaces>
#define SOB_CLASS SimpleOrderbook<StartPrice,DecimalPlaces>

namespace SimpleOrderbook{

template<size_type StartPrice = 50, size_type DecimalPlaces = 3>
class SimpleOrderbook
    : protected Interface{
 /*
  * TODO review how we copy/move/PY_INCREF callbacks
  * TODO time-to-live orders
  * TODO consider storing floating point price as two ints ( base1.base2 ) or ...
  *   a single int that represents the ronded floating point
  */
  static_assert(DecimalPlaces > 0, "DecimalPlaces must be > 0" );
  static_assert(DecimalPlaces < 5, "DecimalPlaces must be < 5" );

  /* generate our lower price/increment values via ratio math */
  typedef std::ratio<1,(size_type)pow(10,DecimalPlaces)>      incr_r;
  typedef std::ratio<StartPrice,1>                            start_r;
  typedef std::ratio_divide<start_r,incr_r>                   raw_incrs_r;
  typedef std::ratio_subtract<raw_incrs_r,std::ratio<1,1>>    low_incrs_r;
  typedef std::ratio_subtract<start_r,
          std::ratio_multiply<low_incrs_r,incr_r>>            base_r;

  /* transalte into scalar values */
  static constexpr size_type lower_increments = floor(low_incrs_r::num /
                                                      low_incrs_r::den);
  static constexpr size_type total_increments = lower_increments * 2 + 1;
  static constexpr size_type minimum_increments = 100;
  static constexpr size_type maximum_increments = 40000; // 3.84MB

  /* check size; stack allocation issues if too large */
  static_assert(total_increments >= minimum_increments, "not enough increments");
  static_assert(total_increments <= maximum_increments, "too many increments");

  /* how callback info is stored in the deferred callback queue */
  typedef std::tuple<fill_callback_type,fill_callback_type,
                     id_type, id_type, price_type,size_type>  dfrd_cb_elem_type;

  /* limit bundle type holds the size and callback of each limit order
   * limit 'chain' type holds all limit orders at a price */
  typedef std::pair<size_type, fill_callback_type>         limit_bndl_type;
  typedef std::map<id_type, limit_bndl_type>               limit_chain_type;

  /* stop bundle type holds the size and callback of each stop order
   * void* to avoid circular typedef of plevel
   * stop 'chain' type holds all stop orders at a price(limit or market) */
  typedef std::tuple<bool,void*,size_type,fill_callback_type>  stop_bndl_type;
  typedef std::map<id_type, stop_bndl_type>                    stop_chain_type;

#define ASSERT_VALID_CHAIN(TYPE) \
    static_assert(std::is_same<TYPE,limit_chain_type>::value || \
                  std::is_same<TYPE,stop_chain_type>::value, \
                  #TYPE " not limit_chain_type or stop_chain_type")

  /* chain pair is the limit and stop chain at a particular price */
  typedef std::pair<limit_chain_type,stop_chain_type>   chain_pair_type ;

  /* an array of all chain pairs (how we reprsent the 'book' internally) */
  typedef std::array<chain_pair_type,total_increments>   order_book_type;
  typedef typename order_book_type::iterator             plevel;

  /* state fields */
  size_type _bid_size, _ask_size, _last_size;

  /* THE ORDER BOOK */
  order_book_type _book;

  /* cached internal pointers(iterators) of the orderbook */
  plevel _beg, _end, _last, _bid, _ask, _low_buy_limit, _high_sell_limit,
        _low_buy_stop, _high_buy_stop, _low_sell_stop, _high_sell_stop;

  large_size_type _total_volume, _last_id;

  /* autonomous market makers */
  std::vector<MarketMaker> _market_makers;

  /* trade has occurred but we've deferred 'handling' it */
  bool _is_dirty;

  /* store deferred callback info until we are clear to execute */
  std::queue<dfrd_cb_elem_type> _deferred_callback_queue;

  /* time & sales */
  std::vector< t_and_s_type > _t_and_s;
  size_type _t_and_s_max_sz;
  bool _t_and_s_full;

  /* user input checks */
  inline bool _check_order_size(size_type sz) const { return sz > 0; }
  inline bool _check_order_price(price_type price) const
  {
    return this->_ptoi(price) < this->_end; // move in 1, Aug21
  }

  /* don't worry about overflow */
  inline large_size_type _generate_id(){ return ++(this->_last_id); }

  /* price-to-index and index-to-price utilities  */
  plevel _ptoi(price_type price) const;
  price_type _itop(plevel plev) const;

  /* calculate chain_size of limit orders at each price level
   * use depth increments on each side of last */
  template< bool BuyNotSell>
  market_depth_type _market_depth(size_type depth) const;

  /* calculate total volume in the chain */
  template< typename ChainTy>
  size_type _chain_size(ChainTy* chain) const;

  /* remove a particular order */
  template< typename ChainTy>
  bool _pull_order(id_type id);

  /* called from _pull order to update cached pointers */
  template<bool BuyStop>
  void _adjust_stop_cache_vals(plevel plev, stop_chain_type* c);
  void _adjust_limit_cache_vals(plevel plev);

  /* dump (to stdout) a particular chain array */
  template< bool BuyNotSell >
  void _dump_limits() const;

  /* dump (to stdout) a particular chain array */
  template< bool BuyNotSell >
  void _dump_stops() const;

  /* handle post-trade tasks */
  void _on_trade_completion();
  void _look_for_triggered_stops();
  template< bool BuyStops>
  void _handle_triggered_stop_chain(plevel plev);

  /* execute if orders match */
  size_type _lift_offers(plevel plev, id_type id, size_type size,
                         fill_callback_type& callback);
  size_type _hit_bids(plevel plev, id_type id, size_type size,
                      fill_callback_type& callback);

  /* signal trade has occurred(admin only, DONT INSERT NEW TRADES FROM HERE!) */
  void _trade_has_occured(plevel plev, size_type size, id_type idbuy,
                          id_type idsell, fill_callback_type& cbbuy,
                          fill_callback_type& cbsell, bool took_offer);

  /* internal insert orders once/if we have an id */
  void _insert_limit_order(bool buy, plevel limit, size_type size,
                            fill_callback_type callback, id_type id);
  void _insert_market_order(bool buy, size_type size,
                             fill_callback_type callback, id_type id);
  void _insert_stop_order(bool buy, plevel stop, size_type size,
                           fill_callback_type callback, id_type id);
  void _insert_stop_order(bool buy, plevel stop, plevel limit, size_type size,
                          fill_callback_type callback, id_type id);

  /* initialize book and market makers; CAREFUL: called from constructor */
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

  id_type insert_limit_order(bool buy, price_type limit, size_type size,
                             fill_callback_type callback);
  id_type insert_market_order(bool buy, size_type size,
                              fill_callback_type callback);
  id_type insert_stop_order(bool buy, price_type stop, size_type size,
                            fill_callback_type callback);
  id_type insert_stop_order(bool buy, price_type stop, price_type limit,
                            size_type size, fill_callback_type callback);

  bool pull_order(id_type id,bool search_limits_first=true);

  id_type replace_with_limit_order(id_type id, bool buy, price_type limit,
                                   size_type size, fill_callback_type callback);
  id_type replace_with_market_order(id_type id, bool buy, size_type size,
                                    fill_callback_type callback);
  id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                  size_type size, fill_callback_type callback);
  id_type replace_with_stop_order(id_type id, bool buy, price_type stop,
                                  price_type limit, size_type size,
                                  fill_callback_type callback);

  inline void dump_buy_limits() const { this->_dump_limits<true>(); }
  inline void dump_sell_limits() const { this->_dump_limits<false>(); }
  inline void dump_buy_stops() const { this->_dump_stops<true>(); }
  inline void dump_sell_stops() const { this->_dump_stops<false>(); }

  void dump_cached_plevels() const;

  inline market_depth_type bid_depth(size_type depth=8) const
  {
    return this->_market_depth<true>(depth);
  }
  inline market_depth_type ask_depth(size_type depth=8) const
  {
    return this->_market_depth<false>(depth);
  }

  inline price_type bid_price() const { return this->_itop(this->_bid); }
  inline price_type ask_price() const { return this->_itop(this->_ask); }
  inline price_type last_price() const { return this->_itop(this->_last); }
  inline size_type bid_size() const { return this->_bid_size; }
  inline size_type ask_size() const { return this->_ask_size; }
  inline size_type last_size() const { return this->_last_size; }
  inline large_size_type volume() const { return this->_total_volume; }
  inline large_size_type last_id() const { return this->_last_id; }

  inline const time_and_sales_type& time_and_sales() const
  {
    return this->_t_and_s;
  }

  /* convert time & sales chrono timepoint to str via ctime */
  static std::string timestamp_to_str(const time_stamp_type& tp);
};

};

};

#include "simple_orderbook.tpp"

#endif
