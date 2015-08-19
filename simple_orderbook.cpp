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
#include <chrono>

namespace NativeLayer{

MarketMaker::MarketMaker(size_type sz_low,
                         size_type sz_high,
                         const SimpleOrderbook& vob)
  :
  _sz_low(sz_low),
  _sz_high(sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(sz_low, sz_high),
  _my_vob(vob)
    {
    }

MarketMaker::MarketMaker(const MarketMaker& mm)
  :
  _sz_low(mm._sz_low),
  _sz_high(mm._sz_high),
  _rand_engine( (clock_type::now() - MarketMaker::seedtp).count() *
                (unsigned long long)this % std::numeric_limits<long>::max()),
  _distr(mm._sz_low, mm._sz_high),
  _my_vob(mm._my_vob)
    {

    }

limit_order_type MarketMaker::post_bid(price_type price)
{
  return limit_order_type(price, this->_distr(this->_rand_engine));
}

limit_order_type MarketMaker::post_ask(price_type price)
{
  return limit_order_type(price, this->_distr(this->_rand_engine));
}

void MarketMaker::default_callback(id_type id,
                                   price_type price,
                                   size_type size)
{
  std::cout<<"MM FILL: "<<' '<<id<<' '<<price<<' '<<size<<std::endl;
}

const clock_type::time_point MarketMaker::seedtp = clock_type::now();

void SimpleOrderbook::_on_trade_completion()
{
  if(this->_is_dirty){
    this->_is_dirty = false;
    while(!this->_deferred_callback_queue.empty()){
      dfrd_cb_elem_type e = this->_deferred_callback_queue.front();
      std::get<0>(e)(std::get<2>(e),std::get<4>(e),std::get<5>(e));
      std::get<1>(e)(std::get<3>(e),std::get<4>(e),std::get<5>(e));
      this->_deferred_callback_queue.pop();
    }
    this->_look_for_triggered_stops();
  }
}

void SimpleOrderbook::_trade_has_occured(price_type price,
                                          size_type size,
                                          id_type id_buyer,
                                          id_type id_seller,
                                          fill_callback_type& cb_buyer,
                                          fill_callback_type& cb_seller,
                                          bool took_offer)
{/*
  * CAREFUL: we can't insert orders from here since we have yet to finish
  * processing the initial order (possible infinite loop);
  *
  * adjust state and use _on_trade_completion() method for earliest insert
  */
  this->_deferred_callback_queue.push(
    dfrd_cb_elem_type(cb_buyer, cb_seller, id_buyer, id_seller, price, size));

  if(this->_t_and_s_full)
    this->_t_and_s.pop_back();
  else if(this->_t_and_s.size() >= (this->_t_and_s_max_sz - 1))
    this->_t_and_s_full = true;

  this->_t_and_s.push_back(t_and_s_type(clock_type::now(),price,size));

  this->_last_price = price;
  this->_total_volume += size;
  this->_last_size = size;
  this->_is_dirty = true;
}

/*
 ************************************************************************
 ************************************************************************
 *** CURRENTLY working under the constraint that stop priority goes:  ***
 ***   low price to high for buys                                     ***
 ***   high price to low for sells                                    ***
 ***   buys before sells                                              ***
 ***                                                                  ***
 *** The other possibility is FIFO irrespective of price              ***
 ************************************************************************
 ************************************************************************
 */

void SimpleOrderbook::_look_for_triggered_stops()
{
  price_type low, high;

  for( low = this->_low_buy_stop ;
       (this->_last_price - low) > -this->_incr_err ;
       low = this->_align(low + this->_incr))
  { /*
     * low >= lowest stop order with error allowance
     * we don't check against max/min, because of the cached high/lows
     */
    this->_handle_triggered_stop_chain(low,false);
  }

  for( high = this->_high_sell_stop ;
       (this->_last_price - high) < this->_incr_err ;
       high = this->_align(high - this->_incr))
  {  /*
     * high <= lowest stop order with error allowance
     * we don't check against max/min, because of the cached high/lows
     */
    this->_handle_triggered_stop_chain(high,true);
  }
}

void SimpleOrderbook::_handle_triggered_stop_chain(price_type price,
                                                    bool ask_side)
{
  stop_chain_type *pchain, cchain;
  price_type limit;

  pchain = this->_find_order_chain(ask_side ? this->_ask_stops
                                            : this->_bid_stops, price);
  /*
   * need to copy the relevant chain, delete original, THEN insert
   * if not we can hit the same order more than once / go into infinite loop
   */
  cchain = stop_chain_type(*pchain);
  pchain->clear();

  if(!cchain.empty()){
   /*
    * need to do this before we potentially recurse into new orders
    */
    if(ask_side)
      this->_high_ask_stop = this->_align(price - this->_incr);
    else
      this->_low_buy_stop = this->_align(price + this->_incr);
  }

  for(stop_chain_type::value_type& elem : cchain){
    limit = elem.second.first.first;
   /*
    * note below we are calling the private versions of _insert,
    * so we can use the old order id as the new one; this allows caller
    * to maintain control via the same order id
    */
    if(limit > 0)
      this->_insert_limit_order(!ask_side, limit, elem.second.first.second,
                                 elem.second.second, elem.first);
    else
      this->_insert_market_order(!ask_side, elem.second.first.second,
                                 elem.second.second, elem.first);
  }
}

/*
 ****************************************************************************
 ****************************************************************************
 *** _lift_offers / _hit_bids are the guts of order execution: attempting ***
 *** to match limit/market orders against the order book, adjusting       ***
 *** state, checking for overflows, signaling etc.                        ***
 ***                                                                      ***
 *** 1) All the corner cases haven't been tested                          ***
 *** 2) Code is quite kludgy and overwritten, needs to be cleaned-up      ***
 ****************************************************************************
 ****************************************************************************
 */

size_type SimpleOrderbook::_lift_offers(price_type price,
                                       id_type id,
                                       size_type size,
                                       fill_callback_type& callback)
{
  limit_chain_type *piask, *pmax;
  limit_chain_type::iterator del_iter;
  size_type amount, for_sale;
  price_type inside_price;

  bool tflag = false;
  long rmndr = 0;
  inside_price = this->_ask_price;
  piask = nullptr;

         /* price >= inside_price (or <= 0) with error allowance */
  while( ((price - inside_price) > -this->_incr_err || price <= 0)
         && size > 0
         /* inside_price <= max_price with error allowance */
         && (inside_price - this->_max_price) < this->_incr_err )
  {
    piask = this->_find_order_chain(this->_ask_limits,inside_price);
    del_iter = piask->begin();

    for(limit_chain_type::value_type& elem : *piask){
      /* check each order , FIFO, for that price level
       * if here we must fill something */
      for_sale = elem.second.first;
      rmndr = size - for_sale;
      amount = std::min(size,for_sale);
      this->_trade_has_occured(inside_price, amount, id, elem.first,
                               callback, elem.second.second, true);
      tflag = true;
      /* reduce the amount left to trade */
      size -= amount;
      /* if we don't need all adjust the outstanding order size,
       * otherwise indicate order should be removed from the maping */
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;
      /* if we have nothing left, break early */
      if(size <= 0)
        break;
    }
    piask->erase(piask->begin(),del_iter);

    try{
      /* incase a market order runs the whole array, past max */
      inside_price = this->_align(inside_price + this->_incr);
    }catch(std::range_error& e){
      this->_ask_price = inside_price;
      this->_ask_size = this->_chain_size(this->_ask_limits, this->_ask_price);
      break;
    }

    if(piask->empty())
      this->_ask_price = inside_price;

    if(tflag){
      tflag = false;
      this->_ask_size = this->_chain_size(this->_ask_limits, this->_ask_price);
    }
  }
  /* if we finish on an empty chain look for one that isn't */
  if(piask && piask->empty())
  {
    for( ++piask, pmax = &(this->_ask_limits[this->_full_range-1]) ;
         piask->empty() && piask < pmax ;
         ++piask)
    {
      inside_price = this->_align(inside_price + this->_incr);
    }
    this->_ask_price = inside_price;
    this->_ask_size = this->_chain_size(this->_ask_limits, this->_ask_price);
  }
  return size; /* what we couldn't fill */
}

size_type SimpleOrderbook::_hit_bids(price_type price,
                                     id_type id,
                                     size_type size,
                                     fill_callback_type& callback )
{
  limit_chain_type *pibid, *pmin;
  limit_chain_type::iterator del_iter;
  size_type amount, for_bid;
  price_type inside_price;

  bool tflag = false;
  long rmndr = 0;
  inside_price = this->_bid_price;
  pibid = nullptr;

         /* price <= inside_price (or <= 0) with error allowance */
  while( ((price - inside_price) < this->_incr_err || price <= 0)
         && size > 0
         /* inside_price >= max_price with error allowance */
         && (inside_price - this->_min_price) > -this->_incr_err )
  {
    pibid = this->_find_order_chain(this->_bid_limits,inside_price);
    del_iter = pibid->begin();

    for(limit_chain_type::value_type& elem : *pibid){
      /* check each order , FIFO, for that price level
       * if here we must fill something */
      for_bid = elem.second.first;
      rmndr = size - for_bid;
      amount = std::min(size,for_bid);
      this->_trade_has_occured(inside_price, amount, elem.first, id,
                               elem.second.second, callback, false);
      tflag = true;
      /* reduce the amount left to trade */
      size -= amount;
      /* if we don't need all adjust the outstanding order size,
       * otherwise indicate order should be removed from the maping */
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;
      /* if we have nothing left, break early */
      if(size <= 0)
        break;
    }
    pibid->erase(pibid->begin(),del_iter);

    try{
      /* in case a market order runs the whole array, past min*/
      inside_price = this->_align(inside_price - this->_incr);
    }catch(std::range_error& e){
      this->_bid_price = inside_price;
      this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid_price);
      break;
    }

    if(pibid->empty())
      this->_bid_price = inside_price;

    if(tflag){
      tflag = false;
      this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid_price);
    }
  }
  /* if we finish on an empty chain look for one that isn't */
  if(pibid && pibid->empty())
  {
    for( --pibid, pmin = &(this->_bid_limits[0]) ;
         pibid->empty() && pibid > pmin ;
         --pibid)
    {
      inside_price = this->_align(inside_price - this->_incr);
    }
    this->_bid_price = inside_price;
    this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid_price);
  }
  return size; /* what we couldn't fill */
}


size_type SimpleOrderbook::_ptoi(price_type price)
{/*
  * because of internal precision considerations we don't check for valid price;
  * we calculate the index value from it and THEN check for a bad index
  */
  price_diff_type offset = (price - this->_init_price) / this->_incr;
  long long index = round(offset) + this->_lower_range;

  if(index < 0 || ((size_type)index >= this->_full_range))
    throw std::range_error( cat("invalid index from price, ","indx: ",
                                std::to_string(index), " price: ",
                                std::to_string(price)) );
  return index;
}

price_type SimpleOrderbook::_itop(size_type index)
{/*
  * because of internal precision issues we don't check for valid index;
  * we calculate the price from it and THEN check for bad price
  */
  long long offset = index - this->_lower_range;
  price_type price = offset * this->_incr + this->_init_price;

  if(price < 0 || (price > this->_max_price))
    throw std::range_error( cat("invalid price from index, ","indx: ",
                                std::to_string(index), " price: ",
                                std::to_string(price)) );
  return price;
}

void SimpleOrderbook::_init(size_type levels_from_init, size_type end_from_init)
{
  /* (crudely, for the time being,) initialize market makers */
  limit_order_type order;
  size_type aanchor = this->_last_price / this->_incr - 1;

  for(MarketMaker& elem : this->_market_makers)
  {
    for(size_type i = aanchor - levels_from_init;
        i < aanchor -end_from_init;
        ++i){
      order = elem.post_bid(this->_itop(i));
      this->insert_limit_order(true, order.first, order.second,
                               &MarketMaker::default_callback);
    }
    for(size_type i = aanchor + end_from_init;
        i < aanchor + levels_from_init;
        ++i){
      order = elem.post_bid(this->_itop(i));
      this->insert_limit_order(false, order.first, order.second,
                               &MarketMaker::default_callback);
    }
  }
}

void SimpleOrderbook::_insert_limit_order(bool buy,
                                          price_type limit,
                                          size_type size,
                                          fill_callback_type callback,
                                          id_type id)
{
  size_type rmndr = size;
  const limit_array_type& la = buy ? this->_bid_limits : this->_ask_limits;
  /*
   * first look if there are matching orders on the offer side
   * pass ref to callback functor, we'll copy later if necessary
   */
  if(buy && limit >= this->_ask_price)
    rmndr = this->_lift_offers(limit,id,size,callback);
  else if(!buy && limit <= this->_bid_price)
    rmndr = this->_hit_bids(limit,id,size,callback);

  /*
   * then add what remains to bid side; copy callback functor, needs to persist
   */
  if(rmndr > 0){
    limit_chain_type* orders = this->_find_order_chain(la, limit);

    limit_bndl_type bndl = limit_bndl_type(rmndr,callback);
    orders->insert(limit_chain_type::value_type(id,std::move(bndl)));

    if(buy && limit >= this->_bid_price){
      this->_bid_price = limit;
      this->_bid_size = this->_chain_size(this->_bid_limits,limit);
    }else if(!buy && limit <= this->_ask_price){
      this->_ask_price = limit;
      this->_ask_size = this->_chain_size(this->_ask_limits,limit);
    }

    if(buy && limit < this->_low_buy_limit)
      this->_low_buy_limit = limit;
    else if(!buy && limit > this->_high_sell_limit)
      this->_high_sell_limit = limit;
  }

  this->_on_trade_completion();
}


void SimpleOrderbook::_insert_market_order(bool buy,
                                           size_type size,
                                           fill_callback_type callback,
                                           id_type id)
{
  size_type rmndr = size;

  rmndr = buy ? this->_lift_offers(-1,id,size,callback)
              : this->_hit_bids(-1,id,size,callback);
  if(rmndr)
    throw liquidity_exception("market order couldn't fill");

  this->_on_trade_completion();
}

void SimpleOrderbook::_insert_stop_order(bool buy,
                                          price_type stop,
                                          size_type size,
                                          fill_callback_type callback,
                                          id_type id)
{
  this->_insert_stop_order(buy, stop, 0, size, std::move(callback), id);
}

void SimpleOrderbook::_insert_stop_order(bool buy,
                                         price_type stop,
                                         price_type limit,
                                         size_type size,
                                         fill_callback_type callback,
                                         id_type id)
{ /*
   * we need an actual trade @/through the stop, i.e can't assume
   * it's already been triggered by where last/bid/ask is...
   *
   * simply pass the order to the appropriate stop chain
   *
   * copy callback functor, needs to persist
   */
  stop_chain_type* orders =
    this->_find_order_chain(buy ? this->_bid_stops : this->_ask_stops, stop);

  /* use 0 limit price for market order */
  stop_bndl_type bndl = stop_bndl_type( limit_order_type(limit,size),callback);
  orders->insert(stop_chain_type::value_type(id,std::move(bndl)));
  /*
   * we maintain references to the most extreme stop prices so we can
   * avoid searching the entire array for triggered orders
   *
   * adjust cached values if ncessary; (should we just maintain a pointer ??)
   */
  if(buy && stop < this->_low_buy_stop)
    this->_low_buy_stop = stop;
  else if(!buy && stop > this->_high_sell_stop)
    this->_high_sell_stop = stop;

  this->_on_trade_completion();
}


SimpleOrderbook::SimpleOrderbook(price_type price,
                                 price_type incr,
                                 size_type mm_num,
                                 size_type mm_sz_low,
                                 size_type mm_sz_high,
                                 size_type mm_init_levels)
  :
  _incr(incr),
  _incr_err(incr / 10),
  _init_price(price),
  _last_price(price),
  _bid_price(0),         /* min - 1 incr */
  _ask_price(2 * price), /* max + 1 incr */
  _min_price(incr),
  _max_price(2 * price - incr),
  _low_buy_limit(price),
  _high_sell_limit(price),
  _low_buy_stop(2 * price), /* max + 1 incr */
  _high_sell_stop(0),        /* min - 1 incr */
  _bid_size(0),
  _ask_size(0),
  _mm_sz_high(mm_sz_high),
  _mm_sz_low(mm_sz_low),
  _lower_range(ceil(price / incr) - 1),    /* init - low, exclusive of init */
  _full_range(this->_lower_range * 2 + 1), /* high - low, inclusive of init */
  _last_size(0),
  _total_volume(0),
  _last_id(0),
  _bid_limits(new limit_chain_type[this->_full_range],
              [](limit_chain_type* omt){ delete[] omt; }),
  _ask_limits(new limit_chain_type[this->_full_range],
              [](limit_chain_type* omt){ delete[] omt; }),
  _bid_stops(new stop_chain_type[this->_full_range],
             [](stop_chain_type* omt){ delete[] omt; }),
  _ask_stops(new stop_chain_type[this->_full_range],
             [](stop_chain_type* omt){ delete[] omt; }),
  _market_makers(mm_num, MarketMaker(mm_sz_low, mm_sz_high, *this)),
  _is_dirty(false),
  _deferred_callback_queue(),
  _t_and_s(),
  _t_and_s_max_sz(1000),
  _t_and_s_full(false)
  {/*
    * since we explicity check args and throw from here, member
    * allocations must be maintained by smart pointers
    */
    if(price <= 0 || incr <= 0 || mm_num <= 0 ||
      mm_sz_low <= 0 || mm_sz_high <= 0 || mm_init_levels <= 0)
      throw std::invalid_argument("non-positive value(s) received");

    if(incr >= price)
      throw std::invalid_argument("incr >= price");

    if(mm_sz_low > mm_sz_high)
      throw std::invalid_argument("mm_sz_low < mm_sz_high");

    if(this->_lower_range < 1)
      throw std::invalid_argument("ceil(price/incr) < 1");

    this->_t_and_s.reserve(this->_t_and_s_max_sz);
    this->_init(mm_init_levels,2);

    std::cout<< "+ SimpleOrderbook Created\n";
  }

SimpleOrderbook::~SimpleOrderbook()
{
  std::cout<< "- SimpleOrderbook Destroyed\n";
}

id_type SimpleOrderbook::insert_limit_order(bool buy,
                                            price_type limit,
                                            size_type size,
                                            fill_callback_type callback)
{
  id_type id;

  if(!this->_check_order_price(limit))
    throw invalid_order("invalid order price");

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  limit = this->_align( limit );
  id = this->_generate_id();

  this->_insert_limit_order(buy,limit,size,callback,id);
  return id;
}


id_type SimpleOrderbook::insert_market_order(bool buy,
                                             size_type size,
                                             fill_callback_type callback)
{
  id_type id;

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  id = this->_generate_id();

  this->_insert_market_order(buy,size,callback,id);
  return id;
}


id_type SimpleOrderbook::insert_stop_order(bool buy,
                                           price_type stop,
                                           size_type size,
                                           fill_callback_type callback)
{
  return this->insert_stop_order(buy,stop,0,size,callback);
}

id_type SimpleOrderbook::insert_stop_order(bool buy,
                                           price_type stop,
                                           price_type limit,
                                           size_type size,
                                           fill_callback_type callback)
{
  id_type id;

  if(!this->_check_order_price(stop))
    throw invalid_order("invalid stop price");

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  if(limit > 0)
    limit = this->_align(limit);
  stop = this->_align(stop);
  id = this->_generate_id();

  this->_insert_stop_order(buy,stop,limit,size,callback,id);
  return id;
}

bool SimpleOrderbook::pull_order(id_type id)
{
  return this->_remove_order_from_chain_array(this->_bid_limits,id) ||
         this->_remove_order_from_chain_array(this->_ask_limits,id) ||
         this->_remove_order_from_chain_array(this->_bid_stops,id) ||
         this->_remove_order_from_chain_array(this->_ask_stops,id);
}

id_type SimpleOrderbook::replace_with_limit_order(id_type id,
                                                  bool buy,
                                                  price_type limit,
                                                  size_type size,
                                                  fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_limit_order(buy,limit,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_market_order(id_type id,
                                                   bool buy,
                                                   size_type size,
                                                   fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new =  this->insert_market_order(buy,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_stop_order(id_type id,
                                                 bool buy,
                                                 price_type stop,
                                                 size_type size,
                                                 fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_stop_order(buy,stop,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_stop_order(id_type id,
                                                 bool buy,
                                                 price_type stop,
                                                 price_type limit,
                                                 size_type size,
                                                 fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_stop_order(buy,stop,limit,size,callback);
  return id_new;
}


std::string
SimpleOrderbook::timestamp_to_str(const SimpleOrderbook::time_stamp_type& tp)
{
  std::time_t t = clock_type::to_time_t(tp);
  std::string ts = std::ctime(&t);
  ts.resize(ts.size() -1);
  return ts;
}

std::ostream& operator<<(std::ostream& out, limit_order_type lim)
{
  std::cout<< lim.second << ',' << lim.first;
  return out;
}
std::ostream& operator<<(std::ostream& out, stop_order_type stp)
{
  std::cout<< stp.first << ',' << stp.second;  // chain to limit overload
  return out;
}

};
