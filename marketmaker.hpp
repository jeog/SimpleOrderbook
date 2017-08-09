/*
Copyright (C) 2015 Jonathon Ogden         < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.    If not, see http://www.gnu.org/licenses.
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
#include <thread>

#include "interfaces.hpp"
#include "types.hpp"

namespace NativeLayer {

/* forward declrs in types.hpp */

using namespace std::placeholders;

market_makers_type 
operator+(market_makers_type&& l, market_makers_type&& r);

market_makers_type 
operator+(market_makers_type&& l, MarketMaker&& r);

/*
 *   MarketMaker objects are intended to be 'autonomous' objects that
 *   provide liquidity, receive execution callbacks, and respond with new orders.
 *
 *   We've restricted copy/assignment - implementing move semantics - to limit
 *   shared access, protecting the market makers once inside the orderbook.
 *   MarketMaker(s) are moved into an orderbook via add_market_maker(&&)
 *   or add_market_makers(&&). For multiple MarketMakers it's recommended to use
 *   the Factories and/or the + overloads to build a market_makers_type. 
 *
 *   The virtual start function is called by the orderbook when it is ready to begin; 
 *   define this as protected(orderbook is a friend class) to control how initial 
 *   orders are inserted; BE SURE TO CALL DOWN TO THE BASE VERSION BEFORE YOU DO ANYTHING !
 *
 *   MarketMaker::Insert<> is how you insert limit orders into the
 *   SimpleOrderbook::LimitInterface passed in to the start function.
 *
 *   Ideally MarketMaker should be sub-classed and a virtual _exec_callback
 *   defined. But a MarketMaker(object or base class) can be instantiated
 *   with a custom callback.
 *
 *   To prevent the callbacks/insert mechanism from going into an unstable
 *   state (e.g an infinite loop: insert->callback->insert->callback etc.) we
 *   set a ::RECURSE_LIMIT that if exceeded with throw callback_overflow on
 *   a call to insert<>(). It resets the count before throwing so you can catch
 *   this in the callback, clean up, and exit gracefully. We also employ a larger
 *   version that doesn't reset(::TOTAL_RECURSE_LIMIT) that prevents further
 *   recursion until previous callbacks come off the 'stack'
 *
 *   Callbacks are handled internally by struct dynamic_functor which rebinds the
 *   underlying instance when a move occurs. Callbacks are called in this order:
 *
 *       MarketMaker::_base_callback    <- handles internal admin
 *       MarketMaker::_callback_ext     <- (optionally) passed in at construction
 *       MarketMaker::_exec_callback    <- virtual, defined in subclass
 *
 *
 *   Sub-classes of MarketMaker have to define the _move_to_new() private
 *   virtual function (DEFAULT_MOVE_TO_NEW macro provides a default definition)
 *   which steals its own contents, passing them to a new derived object inside a
 *   unique_ptr(pMarketMaker). This is intended to be used internally BUT
 *   (and this not recommended) you can grab a reference - to this or any object
 *   after its been moved but before it gets added to the orderbook - and access
 *   it after it enters the orderbook. This can be useful for debugging if you 
 *   want to externally use the insert<bool> call to inject orders
 *
 *   Its recommend to define factories in sub-classes for creating a
 *   market_makers_type collection that can be moved directly into the orderbook.
 */

class MarketMakerA {
    virtual pMarketMaker 
    _move_to_new() = 0;

public:
    virtual 
    ~MarketMakerA() 
        {
        }

    virtual order_exec_cb_type 
    get_callback() 
    { 
        return nullptr; 
    }
};

#define DEFAULT_MOVE_TO_NEW(CLS) \
virtual pMarketMaker _move_to_new(){ \
    return pMarketMaker(new CLS(std::move(static_cast<CLS&&>(*this)))); \
}

class MarketMaker
        : public MarketMakerA {
    friend SimpleOrderbook::QuarterTick;
    friend SimpleOrderbook::TenthTick;
    friend SimpleOrderbook::ThirtySecondthTick;
    friend SimpleOrderbook::HundredthTick;
    friend SimpleOrderbook::ThousandthTick;
    friend SimpleOrderbook::TenThousandthTick;

    friend market_makers_type 
    operator+(market_makers_type&& l, market_makers_type&& r);

    friend market_makers_type 
    operator+(market_makers_type&& l, MarketMaker&& r);

protected:
    typedef MarketMaker my_base_type;

private:
    struct dynamic_functor{
        MarketMaker* _mm;
        bool _mm_alive;
        order_exec_cb_type _base_f;
        order_exec_cb_type _deriv_f;

    public:
        dynamic_functor(MarketMaker* mm)
            : 
                _mm(mm), 
                _mm_alive(true)
            { 
                this->rebind(mm); 
            }

        void 
        rebind(MarketMaker* mm)
        {
            _mm = mm;
            _base_f = std::bind(&MarketMaker::_base_callback, _mm, _1, _2, _3, _4);
            _deriv_f = std::bind(&MarketMaker::_exec_callback, _mm, _1, _2, _3, _4);
        }

        void 
        operator()(callback_msg msg, 
                   id_type id,
                   price_type price, 
                   size_type size)
        {
            if( !_mm_alive )
                return;

            ++(_mm->_recurse_count);
            ++(_mm->_tot_recurse_count);

            if( _mm->_tot_recurse_count <= MarketMaker::TOTAL_RECURSE_LIMIT )
            {
                _base_f(msg,id,price,size);
                if( _mm->_callback_ext ){
                    _mm->_callback_ext(msg, id, price, size);
                }
                _deriv_f(msg,id,price,size);
            }

            if( _mm->_tot_recurse_count ){
                --(_mm->_tot_recurse_count);
            }
            if( _mm->_recurse_count ){
                --(_mm->_recurse_count);
            }
        }

        inline void 
        kill() 
        { 
            _mm_alive = false; 
        }
    }; /* dynamic_functor */

    typedef std::shared_ptr<dynamic_functor> df_sptr_type;

public:
    struct dynamic_functor_wrap{
        df_sptr_type _df;

    public:
        dynamic_functor_wrap(df_sptr_type df = nullptr) 
            : 
                _df(df) 
            {
            }

        inline void 
        operator()(callback_msg msg, 
                   id_type id,
                   price_type price, 
                   size_type size)
        {
            if( _df )
                _df->operator()(msg,id,price,size);
        }

        inline operator 
        bool()
        { 
            return (bool)_df; 
        }

        inline void 
        kill() 
        { 
            if( _df )
                _df->kill(); 
        }
    }; /* dynamic_functor_wrap */

private:
    typedef std::tuple<bool,price_type,size_type> order_bndl_type;
    typedef std::map<id_type,order_bndl_type> orders_map_type;
    typedef orders_map_type::value_type orders_value_type;

    typedef struct{
        bool is_buy;
        price_type price;
        size_type size;
    }fill_info;

    static const int RECURSE_LIMIT = 5;
    static const int TOTAL_RECURSE_LIMIT = 50;

    NativeLayer::SimpleOrderbook::LimitInterface *_book;

    order_exec_cb_type _callback_ext;
    df_sptr_type _callback;

    orders_map_type _my_orders;

    bool _is_running;

    /* restrictive enough? */
    std::recursive_mutex _mtx;

    fill_info _this_fill;
    fill_info _last_fill;
    price_type _tick;

    size_type _bid_out;
    size_type _offer_out;
    long long _pos;

    int _recurse_count;
    int _tot_recurse_count;

    void 
    _base_callback(callback_msg msg,
                   id_type id, 
                   price_type price,
                   size_type size);

    void
    _on_fill_callback(price_type price, size_type size, id_type id);

    virtual void 
    _exec_callback(callback_msg msg,
                   id_type id, 
                   price_type price,
                   size_type size)
    { 
        /* NULL */ 
    }

    virtual pMarketMaker 
    _move_to_new()
    {
        return pMarketMaker( new MarketMaker(std::move(*this)) );
    }

    MarketMaker(const MarketMaker& mm);
    MarketMaker& operator=(const MarketMaker& mm);
    MarketMaker& operator=(MarketMaker&& mm);

protected:
    inline const orders_map_type& 
    my_orders() const 
    { 
        return this->_my_orders; 
    }

    template<bool BuyNotSell>
    size_type 
    random_remove(price_type minp, id_type this_id);

    /* derived need to call down to start / stop */
    virtual void 
    start(NativeLayer::SimpleOrderbook::LimitInterface *book, 
          price_type implied, 
          price_type tick);

    virtual void
    stop();

    template<bool BuyNotSell>
    void 
    insert(price_type price, size_type size, bool no_order_cb = false);

public:
    typedef std::initializer_list<order_exec_cb_type> init_list_type;

    MarketMaker(order_exec_cb_type callback = nullptr);
    MarketMaker(MarketMaker&& mm) noexcept;

    virtual 
    ~MarketMaker() noexcept
        { /* if alive change state of callback so we don't used freed memory */
            if(_callback) 
                _callback->kill();
        }

    inline bool 
    this_fill_was_buy() const 
    { 
        return _this_fill.is_buy; 
    }

    inline bool 
    last_fill_was_buy() const 
    { 
        return _last_fill.is_buy; 
    }

    inline price_type 
    this_fill_price() const 
    { 
        return _this_fill.price; 
    }

    inline price_type 
    last_fill_price() const 
    { 
        return _last_fill.price; 
    }

    inline size_type 
    this_fill_size() const 
    { 
        return _this_fill.size; 
    }

    inline size_type 
    last_fill_size() const 
    { 
        return _last_fill.size; 
    }

    inline size_type 
    tick_chng() const
    {
        return tick_diff(_this_fill.price, _last_fill.price, tick());
    }

    inline price_type 
    tick() const    
    { 
        return _tick; 
    }

    inline size_type 
    bid_out() const 
    { 
        return _bid_out; 
    }

    inline size_type 
    offer_out() const 
    { 
        return _offer_out; 
    }

    inline size_type 
    pos() const 
    { 
        return _pos; 
    }

    virtual order_exec_cb_type 
    get_callback()
    {
        return dynamic_functor_wrap(_callback);
    }

    static market_makers_type 
    Factory(init_list_type il);

    static market_makers_type 
    Factory(unsigned int n);

    static inline size_type
    tick_diff(price_type p1, price_type p2, price_type t)
    {
        return (p1 - p2) / t;
    }
};


template<bool BuyNotSell>
void 
MarketMaker::insert(price_type price, size_type size, bool no_order_cb)
{
    if( !_is_running ){
        throw invalid_state("market/market-maker is not in a running state");
    }

    std::lock_guard<std::recursive_mutex> rlock(_mtx);
    if( _recurse_count > RECURSE_LIMIT ){
        /*
         * note we are reseting the count; caller can catch and keep going with
         * the recursive calls if they want, we did our part...
         */
        _recurse_count = 0;
        throw callback_overflow("market maker trying to insert after exceeding the"
                                " recursion limit set for the callback stack");
    }

    /* note: following block is all args for ->insert_limit_order  */
    _book->insert_limit_order( 
        /* arg 1 */
        BuyNotSell,
        /* arg 2 */ 
        price,
        /* arg 3 */
        size,
        /* arg 4 */
        (!no_order_cb ? dynamic_functor_wrap(_callback) : dynamic_functor_wrap(nullptr)),
        /* arg 5 */
        [=](id_type id)
        {
           /*
            * the post-insertion / pre-completion callback
            *
            * this guarantees to complete before the standard callbacks for
            * this order can be triggered
            *
            * !! WE CAN NOT INSERT / PULL FROM HERE !!
            */
            if(id == 0){
                throw invalid_order("order could not be inserted");
            }

            _my_orders.insert(
                orders_value_type(
                    id, order_bndl_type(BuyNotSell, price,size)
                )
            );

            if(BuyNotSell){
                _bid_out += size;
            }else{
                _offer_out += size;
            }
        }
    );
}


template<bool BuyNotSell>
size_type 
MarketMaker::random_remove(price_type minp, id_type this_id)
{
    size_type s;
    orders_map_type::const_iterator riter, eiter;

    std::lock_guard<std::recursive_mutex> rlock(_mtx);
    eiter = _my_orders.end();
    riter = std::find_if(
                _my_orders.cbegin(),
                eiter,
                [=](orders_value_type p)
                {                    
                    return (std::get<0>(p.second) == BuyNotSell)
                            && (BuyNotSell ? std::get<1>(p.second) < minp 
                                           : std::get<1>(p.second) > minp)
                            && p.first != this_id ;
                }
            );

    s = 0;
    if(riter != eiter){
        s = std::get<2>(riter->second);
        _book->pull_order(riter->first);
    }
    return s;
}


class MarketMaker_Simple1
        : public MarketMaker {
    size_type _sz;
    size_type _max_pos;

    virtual void
    _exec_callback(callback_msg msg, 
                   id_type id, 
                   price_type price, 
                   size_type size);


    void
    _on_fill_callback(price_type price, size_type size);

    void
    _on_wake_callback(price_type price, size_type size);

    DEFAULT_MOVE_TO_NEW(MarketMaker_Simple1)

    /* disable copy construction */
    MarketMaker_Simple1(const MarketMaker_Simple1& mm);

protected:
    void 
    start(NativeLayer::SimpleOrderbook::LimitInterface *book, 
          price_type implied, 
          price_type tick);

public:
    typedef std::initializer_list<std::pair<size_type,size_type>> init_list_type;

    MarketMaker_Simple1(size_type sz, size_type max_pos);
    MarketMaker_Simple1(MarketMaker_Simple1&& mm) noexcept ;

    virtual 
    ~MarketMaker_Simple1() noexcept 
        {
        }

    static market_makers_type 
    Factory(init_list_type il);

    static market_makers_type 
    Factory(size_type n,size_type sz,size_type max_pos);
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
    size_type _max_pos;
    size_type _lowsz;
    size_type _highsz;
    size_type _midsz;

    std::default_random_engine _rand_engine;
    std::uniform_int_distribution<size_type> _distr;
    std::uniform_int_distribution<size_type> _distr2;
    dispersion _disp;

    DEFAULT_MOVE_TO_NEW(MarketMaker_Random);

    virtual void
    _exec_callback(callback_msg msg, 
                   id_type id, 
                   price_type price, 
                   size_type size);

    void
    _on_buy_fill_callback(price_type price, size_type size, id_type id);

    void
    _on_sell_fill_callback(price_type price, size_type size, id_type id);

    void
    _on_wake_callback(price_type price, size_type size);

    unsigned long long 
    _gen_seed();

    /* disable copy construction */
    MarketMaker_Random(const MarketMaker_Random& mm);

protected:
    void 
    start(NativeLayer::SimpleOrderbook::LimitInterface *book, 
          price_type implied, 
          price_type tick);

public:
    typedef std::tuple<size_type,size_type,size_type,dispersion> init_params_type;
    typedef std::initializer_list<init_params_type> init_list_type;

    MarketMaker_Random(size_type sz_low, 
                       size_type sz_high, 
                       size_type max_pos,
                       dispersion d = dispersion::moderate);

    MarketMaker_Random(MarketMaker_Random&& mm) noexcept;

    virtual 
    ~MarketMaker_Random() noexcept 
        {
        }

    static market_makers_type 
    Factory(init_list_type il);

    static market_makers_type 
    Factory(size_type n, 
            size_type sz_low,
            size_type sz_high, 
            size_type max_pos,
            dispersion d);

private:
    static const clock_type::time_point seedtp;
};



};
#endif
