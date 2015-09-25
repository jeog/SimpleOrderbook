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
#include "../simple_orderbook.hpp"
#include "../types.hpp"
#include <chrono>
#include <cmath>

namespace NativeLayer{

using namespace std::placeholders;

market_makers_type operator+(market_makers_type&& l, market_makers_type&& r)
{ /* see notes in header */
  market_makers_type mms;

  for(auto& m : l)
    mms.push_back(std::move(m));
  for(auto& m: r)
    mms.push_back(std::move(m));

  /* make it explicit we stole them */
  l.clear();
  r.clear();
  return mms;
}
market_makers_type operator+(market_makers_type&& l, MarketMaker&& r)
{ /* see notes in header */
  market_makers_type mms;

  for(auto& m : l)
    mms.push_back(std::move(m));
  mms.push_back(r._move_to_new());

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
    _mtx(),
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
      try{
      ob = this->_my_orders.at(id); /* THROW */
      }catch(...){
        int i = 1;
        ++i;
      }
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

      if(rem <= 0){
        this->_my_orders.erase(id);
       // (*pfout)<<"MM-ERASE-"<<std::to_string(id)<<std::endl;
      }
      else
        this->_my_orders[id] =
          order_bndl_type(this->this_fill_was_buy(), price, rem);
    }
    break;
  case callback_msg::cancel:
    {
      ob = this->_my_orders.at(id); /* THROW */
     // fout<<"MM-CANCEL-"<<std::to_string(id)<<std::endl;

      if(std::get<0>(ob))
        this->_bid_out -= std::get<2>(ob);
      else
        this->_offer_out -= std::get<2>(ob);

      this->_my_orders.erase(id);
    }
    break;
  case callback_msg::stop_to_limit:
    throw not_implemented("stop orders should not be used by market makers!");
 }
}

market_makers_type MarketMaker::Factory(init_list_type il)
{
  market_makers_type mms;
  for(auto& i : il)
    mms.push_back(pMarketMaker(new MarketMaker(i)));
  return mms;
}

market_makers_type MarketMaker::Factory(unsigned int n)
{
  market_makers_type mms;
  while(n--)
    mms.push_back(pMarketMaker(new MarketMaker()));
  return mms;
}


MarketMaker_Simple1::MarketMaker_Simple1(size_type sz, size_type max_pos)
  :
    my_base_type(),
    _sz(sz),
    _max_pos(max_pos)
  {
  }

MarketMaker_Simple1::MarketMaker_Simple1(MarketMaker_Simple1&& mm) noexcept
  : /* my_base takes care of rebinding dynamic functor */
    my_base_type(std::move(mm)),
    _sz(mm._sz),
    _max_pos(mm._max_pos)
  {
  }

void MarketMaker_Simple1::start(sob_iface_type *book,
                                price_type implied,
                                price_type tick)
{
  price_type price;

  my_base_type::start(book,implied,tick);

  for(price = implied + tick ;
      (this->offer_out() + this->_sz - this->pos()) <= this->_max_pos;
      price += tick)
     {
       try{ this->insert<false>(price,this->_sz); }catch(...){ break; }
     }
  for(price = implied - tick;
      (this->bid_out() + this->_sz + this->pos()) <= this->_max_pos;
      price -= tick)
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
        if(size <3) break;

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
      {
        std::cout<<"simple1_exec, "<<"stop_to_limit: "<< std::to_string(size)
                 << " @ " <<std::to_string(price) <<std::endl;
      }
      break;
    }
  }
  catch(invalid_order& e)
  {
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
  for(auto& p : il)
    mms.push_back( pMarketMaker(new MarketMaker_Simple1(p.first,p.second)) );
  return mms;
}

market_makers_type MarketMaker_Simple1::Factory(size_type n,
                                                size_type sz,
                                                size_type max_pos)
{
  market_makers_type mms;
  while(n--)
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
    _midsz( (sz_high-sz_low)/2),
    _rand_engine(this->_gen_seed()),
    _distr(sz_low, sz_high),
    _distr2(1, (int)d),
    _disp(d)
  {
    // add a thread that checks/updates/removes orders to avoid staleness
  }

MarketMaker_Random::MarketMaker_Random(MarketMaker_Random&& mm) noexcept
  : /* my_base takes care of rebinding dynamic functor */
    my_base_type(std::move(mm)),
    _max_pos(mm._max_pos),
    _lowsz(mm._lowsz),
    _highsz(mm._highsz),
    _midsz(mm._midsz),
    _rand_engine(std::move(mm._rand_engine)),
    _distr(std::move(mm._distr)),
    _distr2(std::move(mm._distr2)),
    _disp(mm._disp)
  {
  }

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

  for(price = implied + tick*mod ;
      (this->offer_out() + amt  <= this->_max_pos/2);
      price += mod * tick )
     {
       try{ this->insert<false>(price, amt); }catch(...){ break; }
     }
  for(price = implied - tick*mod ;
      (this->bid_out() + amt <= this->_max_pos/2);
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
  bool skip;

  try{
    switch(msg){
    case callback_msg::fill:
      {
    //    if(size < 5)
     //     break;

        adj = this->tick() * this->_distr2(this->_rand_engine);
        amt = this->_distr(this->_rand_engine);
        skip = false;

        if(this->this_fill_was_buy()){
          if(this->bid_out() + amt + this->pos() > this->_max_pos){
            cumm = rret = this->random_remove<true>(price -adj,id);
            while(cumm < amt){
              if(rret ==0){
                skip = true;
                break;
              }
              rret = this->random_remove<true>(price -adj*3,id);
              cumm += rret;
            }
          }
          if(!skip)
            this->insert<true>(price - adj, amt);
          if(amt > this->_midsz)
            this->insert<false>(price + adj, size);
        }else{
          if(this->offer_out() + amt - this->pos() > this->_max_pos){
            cumm = rret = this->random_remove<false>(price + adj,id);
            while(cumm < amt){
              if(rret ==0){
                skip = true;
                break;
              }
              rret = this->random_remove<false>(price + adj*3,id);
              cumm += rret;
            }
          }
          if(!skip)
            this->insert<false>(price + adj, amt);
          if(amt > this->_midsz)
            this->insert<true>(price - adj, size);
        }
      }
      break;
    case callback_msg::cancel:
      break;
    case callback_msg::stop_to_limit:
      {
      std::cout<<"random_exec, "<<"stop_to_limit: "<< std::to_string(size)
               << " @ " <<std::to_string(price) <<std::endl;
      }
      break;
    }

  }
  catch(invalid_order& e)
  {
    std::cerr<< e.what() << std::endl;
  }
  catch(callback_overflow&)
  {
    std::cerr<< "callback overflow in MarketMaker_Random ::: price: "
             << std::to_string(price) << ", size: " << std::to_string(size)
             << ", id: " << std::to_string(id) << std::endl;
  }
}

void MarketMaker_Random::wake(price_type last)
{
  size_type cumm;
  price_type adj;

  adj = this->tick() * this->_distr2(this->_rand_engine);
  if(last <= adj)
    return;
  cumm = this->random_remove<true>(last - adj,0);
  if(cumm)
    this->insert<true>(last - adj, cumm);
  cumm = this->random_remove<false>(last + adj,0);
  if(cumm)
    this->insert<false>(last + adj, cumm);

}

market_makers_type MarketMaker_Random::Factory(init_list_type il)
{
  market_makers_type mms;
  for(auto& i : il){
    mms.push_back(
      pMarketMaker(new MarketMaker_Random(std::get<0>(i), std::get<1>(i),
                                          std::get<2>(i), std::get<3>(i))) );
  }
  return mms;
}

market_makers_type MarketMaker_Random::Factory(size_type n,
                                               size_type sz_low,
                                               size_type sz_high,
                                               size_type max_pos,
                                               dispersion d)
{
  market_makers_type mms;
  while(n--){
    mms.push_back(
      pMarketMaker(new MarketMaker_Random(sz_low,sz_high,max_pos,d)) );
  }
  return mms;
}
const clock_type::time_point MarketMaker_Random::seedtp = clock_type::now();

};
