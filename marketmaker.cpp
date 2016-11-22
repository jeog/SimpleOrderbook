/*
Copyright (C) 2015 Jonathon Ogden  < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include "marketmaker.hpp"
#include "simpleorderbook.hpp"
#include "types.hpp"

#include <chrono>
#include <cmath>

namespace NativeLayer{

using namespace std::placeholders;


market_makers_type 
operator+(market_makers_type&& l, market_makers_type&& r) /* see notes in header */
{ 
    market_makers_type mms;
    mms.reserve(l.size() + r.size());

    std::move(l.begin(),l.end(),back_inserter(mms));
    std::move(r.begin(),r.end(),back_inserter(mms));

    return mms;
}


market_makers_type 
operator+(market_makers_type&& l, MarketMaker&& r) /* see notes in header */
{ 
    market_makers_type mms;
    mms.reserve(l.size() + 1);

    std::move(l.begin(),l.end(),back_inserter(mms));
    mms.push_back(r._move_to_new());

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


MarketMaker::MarketMaker(MarketMaker&& mm) noexcept /* see notes in header */
    : 
        _book(mm._book),
        _callback_ext( mm._callback_ext ),
        _callback( std::move(mm._callback) ),
        _my_orders( std::move(mm._my_orders) ),
        _is_running(mm._is_running),
        _mtx(),
        _this_fill( std::move(mm._this_fill) ),
        _last_fill( std::move(mm._last_fill) ),
        _tick(mm._tick),
        _bid_out(mm._bid_out),
        _offer_out(mm._offer_out),
        _pos(mm._pos),
        _recurse_count(mm._recurse_count),
        _tot_recurse_count(mm._tot_recurse_count)
    {
        if(&mm == this)
            throw move_error("can't move to ourself");

        _callback->rebind(this);

        mm._book = nullptr;
        mm._callback_ext = nullptr;
        mm._callback = nullptr;
        mm._my_orders.clear();
        mm._is_running = false;
        mm._bid_out = 0;
        mm._offer_out = 0;
        mm._pos = 0;
    }


void 
MarketMaker::start( NativeLayer::SimpleOrderbook::LimitInterface *book,
                    price_type implied,
                    price_type tick )
{
    if(!book)
        throw std::invalid_argument("book can not be null(ptr)");

    _is_running = true;
    _book = book;
    _tick = tick;
}


void 
MarketMaker::stop()
{
    _is_running = false;
    _book = nullptr;
}

void 
MarketMaker::_base_callback( callback_msg msg,
                             id_type id,
                             price_type price,
                             size_type size )
{
    order_bndl_type ob;
    long long rem;

    switch(msg){

    /* FILL */
    case callback_msg::fill:
    {        
        ob = _my_orders.at(id);         
        rem = std::get<2>(ob) - size;
        
        _last_fill = std::move(_this_fill);
        _this_fill = {std::get<0>(ob),price,size};

        if(this_fill_was_buy()){
            _pos += size;
            _bid_out -= size;
        }else{
            _pos -= size;
            _offer_out -= size;
        }

        if(rem <= 0)
            _my_orders.erase(id);        
        else
            _my_orders[id] = order_bndl_type(this_fill_was_buy(), price, rem);        
    }
    break;

    /* CANCEL */
    case callback_msg::cancel:
    {
        ob = _my_orders.at(id); /* THROW */

        if(std::get<0>(ob))
            _bid_out -= std::get<2>(ob);
        else
            _offer_out -= std::get<2>(ob);

        _my_orders.erase(id);
    }
    break;

    /* WAKE */
    case callback_msg::wake:
        break;

    /* STOP TO LIMIT */
    case callback_msg::stop_to_limit:
        throw not_implemented("stop orders should not be used by market makers!");
    }
}


market_makers_type 
MarketMaker::Factory(init_list_type il)
{
    market_makers_type mms;
    for(auto& i : il)
        mms.push_back(pMarketMaker(new MarketMaker(i)));
    return mms;
}


market_makers_type 
MarketMaker::Factory(unsigned int n)
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
    :   /* my_base takes care of rebinding dynamic functor */
        my_base_type(std::move(mm)),
        _sz(mm._sz),
        _max_pos(mm._max_pos)
    {
    }


void 
MarketMaker_Simple1::start(NativeLayer::SimpleOrderbook::LimitInterface *book,
                           price_type implied,
                           price_type tick)
{
    price_type price;

    my_base_type::start(book,implied,tick);

    for( price = implied + tick;
         (offer_out() + _sz - pos()) <= _max_pos;
         price += tick )
    {
        try{ 
            insert<false>(price,_sz); 
        }catch(...){ 
            break; 
        }
    }

    for( price = implied - tick;
         (bid_out() + _sz + pos()) <= _max_pos;
         price -= tick )
    {
        try{ 
            insert<true>(price,_sz); 
        }catch(...){ 
            break; 
        }
    }
}


void 
MarketMaker_Simple1::_exec_callback(callback_msg msg,
                                    id_type id,
                                    price_type price,
                                    size_type size)
{
    price_type t = tick();

    try{
        switch(msg){

        /* FILL */
        case callback_msg::fill:
        {
            if(last_fill_was_buy()){
                insert<false>(price + t, size);
                random_remove<false>(price + t,0);
            }else{
                insert<true>(price - t, size);
                random_remove<true>(price - t,0);
            }
        }
        break;

        /* WAKE */
        case callback_msg::wake:
        {
            if(price <= t)
                return;
            if(pos() < 0)
                random_remove<true>(price- t*3,0);
            else
                random_remove<false>(price + t*3,0);

            if( price > last_fill_price() 
                && ((offer_out() + _sz - pos()) <= _max_pos) )
            {
                insert<false>(price + tick(), _sz);
            }
            else if( price < last_fill_price()
                     && ((bid_out() + _sz + pos()) <= _max_pos) )
            {
                insert<true>(price - tick(), _sz);
            }
        }
        break;

        /* CANCEL */
        case callback_msg::cancel:
            break;

        /* STOP TO LIMIT */
        case callback_msg::stop_to_limit:        
            std::cout<<"simple1_exec, "<<"stop_to_limit: "
                     << std::to_string(size)
                     << " @ " <<std::to_string(price) <<std::endl;            
            break;
        }

    }catch(invalid_order& e){
        std::cerr<< e.what() << std::endl;
    }catch(callback_overflow&){
        std::cerr<< "callback overflow in MarketMaker_Simple1 ::: price: "
                 << std::to_string(price) 
                 << ", size: " << std::to_string(size)
                 << ", id: " << std::to_string(id) << std::endl;
    }
}


market_makers_type 
MarketMaker_Simple1::Factory(init_list_type il)
{
    market_makers_type mms;
    for(auto& p : il)
        mms.push_back( pMarketMaker(new MarketMaker_Simple1(p.first,p.second)) );
    return mms;
}


market_makers_type 
MarketMaker_Simple1::Factory(size_type n, size_type sz, size_type max_pos)
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
        _rand_engine(_gen_seed()),
        _distr(sz_low, sz_high),
        _distr2(1, (int)d),
        _disp(d)
    {
    }


MarketMaker_Random::MarketMaker_Random(MarketMaker_Random&& mm) noexcept
    :   
        my_base_type(std::move(mm)), /* my_base takes care of rebinding dynamic functor */
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


unsigned long long 
MarketMaker_Random::_gen_seed()
{
    auto t = (clock_type::now() - MarketMaker_Random::seedtp).count(); 
    return t * (unsigned long long)this % std::numeric_limits<long>::max();
}


void 
MarketMaker_Random::start(NativeLayer::SimpleOrderbook::LimitInterface *book,
                          price_type implied,
                          price_type tick)
{
    size_type mod, amt;
    price_type price;

    my_base_type::start(book,implied,tick);

    mod = _distr2(_rand_engine);
    amt = _distr(_rand_engine);

    for( price = implied + tick * mod;
         (offer_out() + amt) <= _max_pos;
         price += (mod * tick) )
    {
        try{ 
            insert<false>(price, amt); 
        }catch(...){ 
            break; 
        }
    }

    for( price = implied - tick * mod;
         (bid_out() + amt) <= _max_pos;
         price -= (mod * tick) )
    {
        try{ insert<true>(price, amt); }catch(...){ break; }
    }
}


void 
MarketMaker_Random::_exec_callback(callback_msg msg,
                                   id_type id,
                                   price_type price,
                                   size_type size)
{
    price_type t;
    size_type amt, rret, cumm;
    bool skip;

    try{
        switch(msg){

        /* FILL */
        case callback_msg::fill:
        {
            t = tick() * _distr2(_rand_engine);
            amt = _distr(_rand_engine);
            skip = false;

            if(this_fill_was_buy()){
                if(bid_out() + amt + pos() > _max_pos)
                {
                    cumm = rret = random_remove<true>(price -t,id);
                    while(cumm < amt){
                        if(rret ==0){
                            skip = true;
                            break;
                        }
                        rret = random_remove<true>(price -t*3,id);
                        cumm += rret;
                    }
                }

                if(!skip)
                    insert<true>(price - t, amt);

                if(offer_out() + amt - pos() < _max_pos)
                    insert<false>(price + t, size);

            }else{
                if(offer_out() + amt - pos() > _max_pos)
                {
                    cumm = rret = random_remove<false>(price + t,id);
                    while(cumm < amt){
                        if(rret ==0){
                            skip = true;
                            break;
                        }
                        rret = random_remove<false>(price + t*3,id);
                        cumm += rret;
                    }
                }

                if(!skip)
                    insert<false>(price + t, amt);

                if(bid_out() + amt + pos() < _max_pos)
                    insert<true>(price - t, size);
            }
        }
        break;

        /* CANCEL */
        case callback_msg::cancel:
            break;

        /* STOP TO LIMIT */
        case callback_msg::stop_to_limit:           
            std::cout<<"random_exec, "<<"stop_to_limit: "
                     << std::to_string(size)
                     << " @ " <<std::to_string(price) <<std::endl;            
            break;

        /* WAKE */
        case callback_msg::wake:
        {
            t = tick() * _distr2(_rand_engine);
            if(price <= t)
                return;

            if(pos() < 0){
                cumm = random_remove<true>(price- t*2,0);
                if(cumm)
                    insert<true>(price - t, cumm);
            }else{
                cumm = random_remove<false>(price + t*2,0);
                if(cumm)
                 insert<false>(price + t, cumm);
            }
        }
        break;

        }
    }catch(invalid_order& e){
        std::cerr<< e.what() << std::endl;
    }catch(callback_overflow&){
        std::cerr<< "callback overflow in MarketMaker_Random ::: price: "
                 << std::to_string(price) 
                 << ", size: " << std::to_string(size)
                 << ", id: " << std::to_string(id) << std::endl;
    }
}


market_makers_type 
MarketMaker_Random::Factory(init_list_type il)
{
    market_makers_type mms;
    for(auto& i : il){
        mms.push_back(
            pMarketMaker(
                new MarketMaker_Random(std::get<0>(i), 
                                       std::get<1>(i),
                                       std::get<2>(i), 
                                       std::get<3>(i))
             ) 
        );
    }
    return mms;
}


market_makers_type 
MarketMaker_Random::Factory(size_type n,
                            size_type sz_low,
                            size_type sz_high,
                            size_type max_pos,
                            dispersion d)
{
    market_makers_type mms;
    while(n--){
        mms.push_back(
            pMarketMaker(
                new MarketMaker_Random(sz_low, sz_high, max_pos, d)
            ) 
        );
    }
    return mms;
}

const clock_type::time_point MarketMaker_Random::seedtp = clock_type::now();

};


