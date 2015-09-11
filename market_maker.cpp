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

#include "market_maker.hpp"
#include "simple_orderbook.hpp"
#include "types.hpp"
#include <chrono>
#include <cmath>

namespace NativeLayer{

using namespace std::placeholders;

market_makers_type operator+(market_makers_type&& l, market_makers_type&& r)
{ /* see notes in header */
  market_makers_type mms;

  for( auto& m : l)
    mms.push_back( std::move(m));
  for( auto& m: r)
    mms.push_back( std::move(m));
  /* make it explicit we stole them */
  l.clear();
  r.clear();

  return mms;
}
market_makers_type operator+(market_makers_type&& l, MarketMaker&& r)
{ /* see notes in header */
  market_makers_type mms;

  for( auto& m : l)
    mms.push_back( std::move(m));

  mms.push_back( r._move_to_new() );
  l.clear();

  return mms;
}

MarketMaker::MarketMaker(order_exec_cb_type callback)
  :
    _book(nullptr),
    _callback_ext(callback),
    _callback( new dynamic_functor(this) ),
    _is_running(false),
    _this_fill({true,0,0}),
    _last_fill({true,0,0}),
    _tick(0),
    _bid_out(0),
    _offer_out(0),
    _pos(0),
    _recurse_count(0),
    _tot_recurse_count(0)
  {
  }

MarketMaker::MarketMaker(MarketMaker&& mm) noexcept
  : /* see notes in header */
    _book(mm._book),
    _callback_ext( mm._callback_ext ),
    _callback( std::move(mm._callback) ),
    _my_orders( std::move(mm._my_orders) ),
    _is_running(mm._is_running),
    _this_fill(std::move(mm._this_fill)),
    _last_fill(std::move(mm._last_fill)),
    _tick(mm._tick),
    _bid_out(mm._bid_out),
    _offer_out(mm._offer_out),
    _pos(mm._pos),
    _recurse_count(mm._recurse_count),
    _tot_recurse_count(mm._tot_recurse_count)
  {
    if(&mm == this)
      throw move_error("can't move to ourself");
    this->_callback->rebind(this);
    mm._book = nullptr;
    mm._callback_ext = nullptr;
    mm._callback = nullptr;
    mm._my_orders.clear();
    mm._is_running = false;
    mm._bid_out = mm._offer_out = mm._pos = 0;
  }

void MarketMaker::start(sob_iface_type *book,price_type implied,price_type tick)
{
  if(!book)
    throw std::invalid_argument("book can not be null(ptr)");

  this->_is_running = true;
  this->_book = book;
  this->_tick = tick;
}

void MarketMaker::stop()
{
  this->_is_running = false;
  this->_book = nullptr;
}

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

      this->_my_orders.insert(
        std::move(orders_value_type(id,order_bndl_type(BuyNotSell,price,size))));

      if(BuyNotSell) this->_bid_out += size;
      else           this->_offer_out += size;
    });
}


void MarketMaker::_base_callback(callback_msg msg,
                                 id_type id,
                                 price_type price,
                                 size_type size)
{
  order_bndl_type ob;
  long long rem;

  switch(msg){
  case callback_msg::fill:
    {
      std::cout<< "fill: " << std::to_string(id) << std::endl;
      ob = this->_my_orders.at(id); /* THROW */
      rem = std::get<2>(ob) - size;

      /* for our derived class */
      this->_last_fill = std::move(this->_this_fill);
      this->_this_fill = {std::get<0>(ob),price,size};

      if(this->this_fill_was_buy()){
        this->_pos += size;
        this->_bid_out -= size;
      }else{
        this->_pos -= size;
        this->_offer_out -= size;
      }

      if(rem <= 0)
        this->_my_orders.erase(id);
      else
        this->_my_orders[id] =
          order_bndl_type(this->this_fill_was_buy(), price, rem);
    }
    break;
  case callback_msg::cancel:
    std::cout<< "cancel: " << std::to_string(id) << std::endl;
  //           << std::boolalpha << std::get<0>(this->_my_orders.at(id)) << '\n';

    /// THIS IS CAUSING .at(id) to throw
    this->_my_orders.erase(id);
    break;
  case callback_msg::stop_to_limit:
    std::cout<<"base, "<<"stop_to_limit: "<< std::to_string(size) << " @ "
             <<std::to_string(price) <<std::endl;
    break;
  }
}

//id_type MarketMaker::high_price_order()
//{
  /* do we want to implement O(n) search or rethink how we store orders ?? */
//}
//id_type MarketMaker::low_price_order()
//{

//}

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
                            && (BuyNotSell ? std::get<1>(p.second) <= minp
                                           : std::get<1>(p.second) >= minp)
                            && p.first != this_id ;
                     });
  s = 0;
  if(riter != eiter){
    s = std::get<2>(riter->second);
    std::cout<< "pulling: " << std::to_string(riter->first) << std::endl;
    this->_book->pull_order(riter->first);
  }
  return s;
}

market_makers_type MarketMaker::Factory(init_list_type il)
{
  market_makers_type mms;
  for( auto& i : il )
    mms.push_back( pMarketMaker(new MarketMaker(i)) );

  return mms;
}

market_makers_type MarketMaker::Factory(unsigned int n)
{
  market_makers_type mms;
  while( n-- )
    mms.push_back( pMarketMaker(new MarketMaker()) );

  return mms;
}

/***/

MarketMaker_Simple1::MarketMaker_Simple1(size_type sz, size_type max_pos)
  :
    my_base_type(),
    _sz(sz),
    _max_pos(max_pos)
  {
  }

MarketMaker_Simple1::MarketMaker_Simple1(MarketMaker_Simple1&& mm) noexcept
  :
    /* my_base takes care of rebinding dynamic functor */
    my_base_type( std::move(mm) ),
    _sz(mm._sz),
    _max_pos(mm._max_pos)
  {
  }

/*
MarketMaker_Simple1::MarketMaker_Simple1(const MarketMaker_Simpe1& mm)
  : *//* call down for new base, with a callback bound to a new 'this' *//*
    my_base_type( std::bind(&MarketMaker_Simple1::_callback,this,_1,_2,_3,_4) ),
    _sz(mm._sz),
    _max_pos(mm._max_pos)
  {
  }*/

void MarketMaker_Simple1::start(sob_iface_type *book,
                                price_type implied,
                                price_type tick)
{
  size_type i;
  price_type price;

  my_base_type::start(book,implied,tick);

  for( i = 0, price = implied + tick ;
       i < 5 && (this->offer_out() + this->_sz - this->pos() <= this->_max_pos);
       price += tick, ++i )
  {
    try{ this->insert<false>(price,this->_sz); }catch(...){ break; }
  }

  for( i = 0, price = implied - tick ;
       i < 5 && (this->bid_out() + this->_sz + this->pos() <= this->_max_pos);
       price -= tick, ++i )
  {
    try{ this->insert<true>(price,this->_sz); }catch(...){ break; }
  }
}


void MarketMaker_Simple1::_exec_callback(callback_msg msg,
                                         id_type id,
                                         price_type price,
                                         size_type size)
{
  try{
    switch(msg){
    case callback_msg::fill:
      {
      //  if(this->this_fill_size() < this->last_fill_size())
     //     break;
        if(size < 3){
          if(this->this_fill_was_buy())
            this->insert<false>(price+this->tick(),size*2,true);
          else
            this->insert<true>(price-this->tick(),size*2,true);
          break;
        }

        if(this->this_fill_was_buy())
        {
          if(this->bid_out() + this->_sz + this->pos() > this->_max_pos)
            if(this->random_remove<true>(price - this->tick()*3,id) > 0){
              this->insert<true>(price - this->tick(), (int)(this->_sz/3));
              this->insert<true>(price - this->tick()*2, (int)(this->_sz/3));
              this->insert<true>(price - this->tick()*3, (int)(this->_sz/3));
            }
          this->insert<false>(price + this->tick(), (int)(size/3));
          this->insert<false>(price + this->tick()*2, (int)(size/3));
          this->insert<false>(price + this->tick()*3, (int)(size/3));
        }
        else
        {
          if(this->offer_out() + this->_sz - this->pos() > this->_max_pos)
            if(this->random_remove<false>(price + this->tick()*3,id) > 0){
              this->insert<false>(price + this->tick(), (int)(this->_sz/3));
              this->insert<false>(price + this->tick()*2, (int)(this->_sz/3));
              this->insert<false>(price + this->tick()*3, (int)(this->_sz/3));
            }
          this->insert<true>(price - this->tick(), (int)(size/3));
          this->insert<true>(price - this->tick()*2, (int)(size/3));
          this->insert<true>(price - this->tick()*3, (int)(size/3));
        }
      }
      break;
    case callback_msg::cancel:
      break;
    case callback_msg::stop_to_limit:
      std::cout<<"simple1_exec, "<<"stop_to_limit: "<< std::to_string(size)
               << " @ " <<std::to_string(price) <<std::endl;
      break;
    }
  }
  catch(invalid_order& e){
    std::cerr<< e.what() << std::endl;
  }
  catch(callback_overflow&)
  {
     std::cerr<< "callback overflow in MarketMaker_Simple1 ::: price: "
              << std::to_string(price) << ", size: " << std::to_string(size)
              << ", id: " << std::to_string(id) << std::endl;
  }
}

market_makers_type MarketMaker_Simple1::Factory(init_list_type il)
{
  market_makers_type mms;

  for( auto& p : il )
    mms.push_back( pMarketMaker(new MarketMaker_Simple1(p.first,p.second)) );

  return mms;
}

market_makers_type MarketMaker_Simple1::Factory(size_type n,
                                                size_type sz,
                                                size_type max_pos)
{
  market_makers_type mms;

  while( n-- )
    mms.push_back( pMarketMaker(new MarketMaker_Simple1(sz,max_pos)) );

  return mms;
}




MarketMaker_Random::MarketMaker_Random(size_type sz_low,
                                       size_type sz_high,
                                       size_type max_pos,
                                       MarketMaker_Random::dispersion d)
  :
    my_base_type(),
    _max_pos(max_pos),
    _lowsz(sz_low),
    _highsz(sz_high),
    _bcumm(0),
    _scumm(0),
    _rand_engine(this->_gen_seed()),
    _distr(sz_low, sz_high),
    _distr2(1, (int)d),
    _disp(d)
  {
  }

MarketMaker_Random::MarketMaker_Random(MarketMaker_Random&& mm) noexcept
  :
    /* my_base takes care of rebinding dynamic functor */
    my_base_type( std::move(mm) ),
    _max_pos(mm._max_pos),
    _lowsz(mm._lowsz),
    _highsz(mm._highsz),
    _bcumm(mm._bcumm),
    _scumm(mm._scumm),
    _rand_engine( std::move(mm._rand_engine) ),
    _distr( std::move(mm._distr) ),
    _distr2( std::move(mm._distr2) ),
    _disp( mm._disp )
  {
  }

/*
MarketMaker_Random::MarketMaker_Random(const MarketMaker_Random& mm)
  : *//* call down for new base, with a callback bound to a new 'this' *//*
    my_base_type( std::bind(&MarketMaker_Random::_callback,this,_1,_2,_3,_4) ),
    _max_pos(mm._max_pos),
    _rand_engine(this->_gen_seed()),
    _distr(mm._distr),
    _distr2(mm._distr2)
  {
  }*/

unsigned long long MarketMaker_Random::_gen_seed()
{
  return (clock_type::now() - MarketMaker_Random::seedtp).count()
         * (unsigned long long)this
         % std::numeric_limits<long>::max();
}

void MarketMaker_Random::start(sob_iface_type *book,
                               price_type implied,
                               price_type tick)
{
  size_type mod, amt;
  price_type price;

  my_base_type::start(book,implied,tick);

  mod = this->_distr2(this->_rand_engine);
  amt = this->_distr(this->_rand_engine);

  for( price = implied + tick*mod ;
       (this->offer_out() + amt  <= this->_max_pos);
       price += mod * tick )
  {
    try{ this->insert<false>(price, amt); }catch(...){ break; }
  }

  for( price = implied - tick*mod ;
       (this->bid_out() + amt <= this->_max_pos);
       price -= mod * tick)
  {
    try{ this->insert<true>(price, amt); }catch(...){ break; }
  }
}

void MarketMaker_Random::_exec_callback(callback_msg msg,
                                        id_type id,
                                        price_type price,
                                        size_type size)
{
  price_type adj;
  size_type amt, rret, cumm; //, part;

  try{
    switch(msg){
    case callback_msg::fill:
      {
/*
        if( abs(this->this_fill_price()-this->last_fill_price())
            < (this->tick() * (int)this->_disp - (.01 * this->tick())) )
        {
          break;
        }
*/
        /*
         * need a way to disable or nullpr the callback,
         *  so we can simulate a market order
         *
         *  also some type of tag/bundle that we can read
         */
   //     if(this->this_fill_was_buy()) this->_bcumm += size;
    //    else                          this->_scumm += size;

        if(size < 5){
          if(this->this_fill_was_buy())
            this->insert<false>(price+this->tick(),size*2,true);
          else
            this->insert<true>(price-this->tick(),size*2,true);
          break;
        }

        adj = this->tick() * this->_distr2(this->_rand_engine);
        amt = this->_distr(this->_rand_engine);

        if(this->bid_out() + amt + this->pos() > this->_max_pos){
          cumm = rret = this->random_remove<true>(price -adj*3,id);
          while(cumm < amt){
            if(rret ==0) goto done_b1;
            rret = this->random_remove<true>(price -adj*3,id);
            cumm += rret;
          }
          this->insert<true>(price - adj, amt);
        }

        done_b1:

        if(this->offer_out() + amt - this->pos() > this->_max_pos){
          cumm = rret = this->random_remove<false>(price -adj*3,id);
          while(cumm < amt){
            if(rret ==0) break;
            rret = this->random_remove<true>(price -adj*3,id);
            cumm += rret;
          }
          this->insert<false>(price + adj, amt);
        }

      }
      break;
    case callback_msg::cancel:
      break;
    case callback_msg::stop_to_limit:
      std::cout<<"random_exec, "<<"stop_to_limit: "<< std::to_string(size)
               << " @ " <<std::to_string(price) <<std::endl;
      break;
    }

  }
  catch(invalid_order& e){
    std::cerr<< e.what() << std::endl;
  }
  catch(callback_overflow&)
  {
    std::cerr<< "callback overflow in MarketMaker_Random ::: price: "
             << std::to_string(price) << ", size: " << std::to_string(size)
             << ", id: " << std::to_string(id) << std::endl;
  }
}

market_makers_type MarketMaker_Random::Factory(init_list_type il)
{
  market_makers_type mms;
  for( auto& i : il )
    mms.push_back(
      pMarketMaker(new MarketMaker_Random(std::get<0>(i), std::get<1>(i),
                                          std::get<2>(i), std::get<3>(i))) );

  return mms;
}

market_makers_type MarketMaker_Random::Factory(size_type n,
                                               size_type sz_low,
                                               size_type sz_high,
                                               size_type max_pos,
                                               dispersion d)
{
  market_makers_type mms;
  while( n-- )
    mms.push_back(
      pMarketMaker(new MarketMaker_Random(sz_low,sz_high,max_pos,d)) );

  return mms;
}
const clock_type::time_point MarketMaker_Random::seedtp = clock_type::now();

};
