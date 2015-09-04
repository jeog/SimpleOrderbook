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

  mms.push_back( r.move_to_new(std::move(r)) );
  l.clear();

  return mms;
}

MarketMaker::MarketMaker(callback_type callback) //callback_type callback)
  :
    _book(nullptr),
    _callback_ext(callback),
    _callback( new dynamic_functor(this) ),
    _is_running(false),
    _last_was_buy(true),
    _tick(0),
    _bid_out(0),
    _offer_out(0),
    _pos(0)
  {
  }

MarketMaker::MarketMaker(MarketMaker&& mm) noexcept
  : /* see notes in header */
    _book(mm._book),
    _callback_ext( mm._callback_ext ),
    _callback( std::move(mm._callback) ),
    _my_orders( std::move(mm._my_orders) ),
    _is_running(mm._is_running),
    _last_was_buy(mm._last_was_buy),
    _tick(mm._tick),
    _bid_out(mm._bid_out),
    _offer_out(mm._offer_out),
    _pos(mm._pos)
  {
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
void MarketMaker::insert(price_type price, size_type size)
{
  if(!this->_is_running)
    throw invalid_state("market/market-maker is not in a running state");

                                         // DOES THIS DEFEAT THE PURPOSE ????
  this->_book->insert_limit_order(BuyNotSell,price,size,*(this->_callback.get()),
    /*
     * the post-insertion / pre-completion callback
     *
     * this guarantees to complete before a standard callback for
     * this order can be triggered
     */
    [=](id_type id)
    {
      if(id == 0)
        throw invalid_order("order could not be inserted");

      this->_my_orders.insert(
        std::move(orders_value_type(id,order_bndl_type(BuyNotSell,price,size))));

      if(BuyNotSell)
        this->_bid_out += size;
      else
        this->_offer_out += size;
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
      ob = this->_my_orders.at(id); /* THROW */
      rem = std::get<2>(ob) - size;

      /* for our derived class */
      this->_last_was_buy = std::get<0>(ob);
      if(this->_last_was_buy){
        this->_pos += size;
        this->_bid_out -= size;
      }else{
        this->_pos -= size;
        this->_offer_out -= size;
      }

      if(rem <= 0)
        this->_my_orders.erase(id);
      else
        this->_my_orders[id] = order_bndl_type(this->_last_was_buy,price,rem);
    }
    break;
  case callback_msg::cancel:
    this->_my_orders.erase(id);
    break;
  case callback_msg::shutdown:
    this->stop();
    break;
  }
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
    this->insert<false>(price,this->_sz);
  }

  for( i = 0, price = implied - tick ;
       i < 5 && (this->bid_out() + this->_sz + this->pos() <= this->_max_pos);
       price -= tick, ++i )
  {
    this->insert<true>(price,this->_sz);
  }
}


void MarketMaker_Simple1::_exec_callback(callback_msg msg,
                                        id_type id,
                                        price_type price,
                                        size_type size)
{
  switch(msg){
  case callback_msg::fill:
    {
      if(this->last_was_buy())
      {
        if(this->bid_out() + this->_sz + this->pos() <= this->_max_pos)
          this->insert<true>(price - this->tick(), this->_sz);
        this->insert<false>(price + (2*this->tick()), size);
      }
      else
      {
        if(this->offer_out() + this->_sz - this->pos() <= this->_max_pos)
          this->insert<false>(price + this->tick(), this->_sz);
        this->insert<true>(price - (2*this->tick()), size);
      }
    }
    break;
  case callback_msg::cancel:
    break;
  case callback_msg::shutdown:
    break;
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
    _rand_engine(this->_gen_seed()),
    _distr(sz_low, sz_high),
    _distr2(1, (int)d)
  {
  }

MarketMaker_Random::MarketMaker_Random(MarketMaker_Random&& mm) noexcept
  :
    /* my_base takes care of rebinding dynamic functor */
    my_base_type( std::move(mm) ),
    _max_pos(mm._max_pos),
    _rand_engine( std::move(mm._rand_engine) ),
    _distr( std::move(mm._distr) ),
    _distr2( std::move(mm._distr2) )
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
  size_type mod, count, i, amt;
  price_type price;

  my_base_type::start(book,implied,tick);

  mod = this->_distr2(this->_rand_engine);
  count = this->_distr2(this->_rand_engine);
  amt = this->_distr(this->_rand_engine);

  for( i = 0, price = implied + tick ;
       i < count && (this->offer_out() + amt - this->pos() <= this->_max_pos);
       price += mod * tick, ++i )
  { /* insert some random sell-limits */
    this->insert<false>(price, amt);
  }

  for( i = 0, price = implied - tick ;
       i < count && (this->bid_out() + amt + this->pos() <= this->_max_pos);
       price -= mod * tick, ++i )
  { /* insert some random buy-limits */
    this->insert<true>(price, amt);
  }
}

void MarketMaker_Random::_exec_callback(callback_msg msg,
                                        id_type id,
                                        price_type price,
                                        size_type size)
{
  price_type adj;
  size_type amt;
  switch(msg){
  case callback_msg::fill:
    {
      adj = this->tick() * this->_distr2(this->_rand_engine);
      amt = this->_distr(this->_rand_engine);

      if(this->last_was_buy()){
        if(this->bid_out() + amt + this->pos() <= this->_max_pos)
          this->insert<true>(price - adj, amt);
        if(this->offer_out() + amt - this->pos() <= this->_max_pos)
          this->insert<false>(price + adj, size);
        /* these added calls are causing segfault ! */
   //     this->insert<false>(price + this->_tick, 1);
   //     this->insert<false>(price , 1);

      }else{
        if(this->offer_out() + amt - this->pos() <= this->_max_pos)
          this->insert<false>(price + adj, amt);
        if(this->bid_out() + amt + this->pos() <= this->_max_pos)
          this->insert<true>(price - adj, size);
      //  this->insert<true>(price ,  1);
    //    this->insert<true>(price - this->_tick,  1);

      }
    }
    break;
  case callback_msg::cancel:
    break;
  case callback_msg::shutdown:
    break;
  }
}

market_makers_type MarketMaker_Random::Factory(init_list_type il)
{
  market_makers_type mms;
  for( auto& t : il )
    mms.push_back( pMarketMaker(new MarketMaker_Random(std::get<0>(t),
                                                       std::get<1>(t),
                                                       std::get<2>(t))));

  return mms;
}

market_makers_type MarketMaker_Random::Factory(size_type n,
                                               size_type sz_low,
                                               size_type sz_high,
                                               size_type max_pos)
{
  market_makers_type mms;
  while( n-- )
    mms.push_back( pMarketMaker(new MarketMaker_Random(sz_low,sz_high,max_pos)));

  return mms;
}
const clock_type::time_point MarketMaker_Random::seedtp = clock_type::now();

};
