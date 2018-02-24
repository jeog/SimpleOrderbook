/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

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

#include <iterator>
#include <iomanip>
#include <climits>
#include "../../include/simpleorderbook.hpp"

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbookImpl(TickPrice<TickRatio> min, size_t incr)
    :
        /* lowest price */
        _base(min),
        /* actual orderbook object */
        _book(incr + 1), /*pad the beg side */
        /***************************************************************
                      *** our ersatz iterator approach ****
                                i = [ 0, incr )
     
         vector iter    [begin()]                               [ end() ]
         internal pntr  [ _base ][ _beg ]           [ _end - 1 ][ _end  ]
         internal index [ NULL  ][   i  ][ i+1 ]...   [ incr-1 ][  NULL ]
         external price [ THROW ][ min  ]              [  max  ][ THROW ]           
        *****************************************************************/
        _beg( &(*_book.begin()) + 1 ), 
        _end( &(*_book.end())),
        /* internal pointers for faster lookups */
        _last( 0 ),  
        _bid( _beg - 1),
        _ask( _end ),
        _low_buy_limit( _end ),
        _high_sell_limit( _beg - 1 ),
        _low_buy_stop( _end ),
        _high_buy_stop( _beg - 1 ),
        _low_sell_stop( _end ),
        _high_sell_stop( _beg - 1 ),
        /* order/id caches for faster lookups */
        _id_cache(),
        _trailing_sell_stops(),
        _trailing_buy_stops(),
        /* internal trade stats */
        _total_volume(0),
        _last_id(0), 
        _last_size(0),
        _timesales(),                  
        /* trade callbacks */
        _deferred_callbacks(), 
        _busy_with_callbacks(false),
        /* our threaded approach to order queuing/exec */
        _order_queue(),
        _order_queue_mtx(),    
        _order_queue_cond(),
        _noutstanding_orders(0),                       
        _need_check_for_stops(false),
        /* core sync objects */
        _master_mtx(),  
        _master_run_flag(true)       
    {                               
        /*** DONT THROW AFTER THIS POINT ***/
        _order_dispatcher_thread = 
            std::thread(std::bind(&SOB_CLASS::_threaded_order_dispatcher,this));               
    }


SOB_TEMPLATE 
SOB_CLASS::~SimpleOrderbookImpl()
    { 
        _master_run_flag = false;       
        try{ 
            {
                std::lock_guard<std::mutex> lock(_order_queue_mtx);
                _order_queue.push(order_queue_elem()); 
                /* don't incr _noutstanding_orders;
                   we break main loop before we can decr */
            }    
            _order_queue_cond.notify_one();
            if( _order_dispatcher_thread.joinable() ){
                _order_dispatcher_thread.join();
            }
        }catch(...){
        }        
    }


SOB_TEMPLATE
void
SOB_CLASS::_threaded_order_dispatcher()
{
    for( ; ; ){
        order_queue_elem e;
        {
            std::unique_lock<std::mutex> lock(_order_queue_mtx);
            _order_queue_cond.wait(
                lock,
                [this]{ return !this->_order_queue.empty(); }
            );

            e = std::move(_order_queue.front());
            _order_queue.pop();
            if( !_master_run_flag ){
                if( _noutstanding_orders != 0 ){
                    throw std::runtime_error("_noutstanding_orders != 0");
                }
                break;
            }
        }

        std::promise<id_type> p = std::move( e.promise );
        if( !e.id ){
            e.id = _generate_id();
        }

        try{
            std::lock_guard<std::mutex> lock(_master_mtx);
            /* --- CRITICAL SECTION --- */
            if( !_insert_order(e) ){
                e.id = 0; // bad pull
            }
            _assert_internal_pointers();
            /* --- CRITICAL SECTION --- */
        }catch(std::exception& e){
            --_noutstanding_orders;
            p.set_exception( std::current_exception() );
            std::cerr << "exception in order dispatcher: "
                      << e.what() << std::endl;
            continue;
        }

        --_noutstanding_orders;
        p.set_value(e.id);
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_insert_order(const order_queue_elem& e)
{
    if( _order::is_advanced(e) ){
        _route_advanced_order(e);
        return true;
    }
    if( _order::is_null(e) ){
        /* not the cleanest but most effective/thread-safe
           success/fail is returned in the in e.id*/
        return _pull_order(e.id, true);
    }
    _route_basic_order<>(e);
    return true;
}


SOB_TEMPLATE
template<side_of_trade side>
fill_type
SOB_CLASS::_route_basic_order(const order_queue_elem& e)
{
    if( side == side_of_trade::both ){
        return e.is_buy ? _route_basic_order<side_of_trade::buy>(e)
                        : _route_basic_order<side_of_trade::sell>(e);
    }
    switch( e.type ){
    case order_type::limit:
        return _insert_limit_order<side == side_of_trade::buy>(e);
    case order_type::market:
        _insert_market_order<side == side_of_trade::buy>(e);
        return fill_type::immediate_full;
    case order_type::stop: /* no break */
    case order_type::stop_limit:
        _insert_stop_order<side == side_of_trade::buy>(e);
        return fill_type::none;
    default:
        throw std::runtime_error("invalid order type in order_queue");
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_inject_order(const order_queue_elem& e, bool partial_ok)
{
    switch( _route_basic_order<>(e) ){
    case fill_type::immediate_full: return true;
    case fill_type::immediate_partial: return partial_ok;
    default: return false;
    }
}


/*
 *  _trade<bool> : the guts of order execution:
 *      match orders against the order book,
 *      adjust internal state,
 *      check for overflows  
 *      if price changed adjust trailing stops
 *      look/execute stop orders
 */
SOB_TEMPLATE 
template<bool BidSide>
size_t 
SOB_CLASS::_trade( plevel plev, 
                   id_type id, 
                   size_t size,
                   const order_exec_cb_type& exec_cb )
{
    plevel old_last = _last;
    while(size){
        /* can we trade at this price level? */
        if( !_core_exec<BidSide>::is_executable_chain(this, plev) ){
            break;   
        }

        /* trade at this price level */
        size = _hit_chain( _core_exec<BidSide>::get_inside(this),
                            id, size, exec_cb );

        /* reset the inside price level (if we can) OR stop */  
        if( !_core_exec<BidSide>::find_new_best_inside(this) ){
            break;
        }
    }

    /*
     * dont need to check that old != 0; in order to have an active
     * trailing_stop we must have had an initial trade
     */
    if( old_last != _last ){
        _adjust_trailing_stops(_last < old_last);
    }

    if( _need_check_for_stops ){
        _look_for_triggered_stops();
    }

    return size; /* what we couldn't fill */
}


/*
 * _hit_chain : handles all the trades at a particular plevel
 *              returns what it couldn't fill  
 */
SOB_TEMPLATE
size_t
SOB_CLASS::_hit_chain( plevel plev,
                       id_type id,
                       size_t size,
                       const order_exec_cb_type& exec_cb )
{
    size_t amount;
    long long rmndr; 
    auto del_iter = plev->first.begin();

    /* check each order, FIFO, for this plevel */
    for( auto& elem : plev->first ){        
        amount = std::min(size, elem.sz);
        /* push callbacks onto queue; update state */
        _trade_has_occured( plev, amount, id, elem.id, exec_cb,
                            elem.exec_cb );

        /* reduce the amount left to trade */ 
        size -= amount;    
        rmndr = elem.sz - amount;

        /* deal with advanced order conditions */
        if( _order::is_advanced(elem) ){
            assert(elem.trigger != condition_trigger::none);
            if( !_order::needs_full_fill(elem) || !rmndr ){
                _handle_advanced_order_cancel(elem, elem.id)
                || _handle_advanced_order_trigger(elem, elem.id);
            }
        }

        /* adjust outstanding order size or indicate removal if none left */
        if( rmndr > 0 ){ 
            elem.sz = rmndr;
        }else{
            _id_cache.erase(elem.id);
            ++del_iter;
        }     

        /* if nothing left to trade*/
        if( size <= 0 ){ 
            break;
        }
    }

    plev->first.erase(plev->first.begin(),del_iter);
    return size;
}


SOB_TEMPLATE
void
SOB_CLASS::_trade_has_occured( plevel plev,
                               size_t size,
                               id_type idbuy,
                               id_type idsell,
                               const order_exec_cb_type& cbbuy,
                               const order_exec_cb_type& cbsell )
{
    /* CAREFUL: we can't insert orders from here since we have yet to finish
       processing the initial order (possible infinite loop); */
    double p = _itop(plev);

    /* buy and sell sides */
    _push_callback(callback_msg::fill, cbbuy, idbuy, idbuy, p, size);
    _push_callback(callback_msg::fill, cbsell, idsell, idsell, p, size);

    _timesales.push_back( std::make_tuple(clock_type::now(), p, size) );
    _last = plev;
    _total_volume += size;
    _last_size = size;
    _need_check_for_stops = true;
}



SOB_TEMPLATE
template<bool BuyLimit>
sob::fill_type
SOB_CLASS::_insert_limit_order(const order_queue_elem& e)
{
    assert( _order::is_limit(e) );
    fill_type fill = fill_type::none;
    plevel p = _ptoi(e.limit);
    size_t rmndr = e.sz;

    if( (BuyLimit && p >= _ask) || (!BuyLimit && p <= _bid) ){
        /* If there are matching orders on the other side fill @ market
           - pass ref to callback functor, we'll copy later if necessary
           - return what we couldn't fill @ market */
        rmndr = _trade<!BuyLimit>(p, e.id, e.sz, e.exec_cb);
    }

    if( rmndr > 0) {
        /* insert what remains as limit order */
        _chain<limit_chain_type>::template
            push<BuyLimit>(this, p, limit_bndl(e.id, rmndr, e.exec_cb) );
        if( rmndr < e.sz ){
            fill = fill_type::immediate_partial;
        }
    }else{
        fill = fill_type::immediate_full;
    }

    return fill;
}


SOB_TEMPLATE
template<bool BuyMarket>
void
SOB_CLASS::_insert_market_order(const order_queue_elem& e)
{
    assert( _order::is_market(e) );
    size_t rmndr = _trade<!BuyMarket>(nullptr, e.id, e.sz, e.exec_cb);
    if( rmndr > 0 ){
        throw liquidity_exception( e.sz, rmndr, e.id);
    }
}


SOB_TEMPLATE
template<bool BuyStop>
void
SOB_CLASS::_insert_stop_order(const order_queue_elem& e)
{
    assert( _order::is_stop(e) );
   /*
    * we need an actual trade @/through the stop, i.e can't assume
    * it's already been triggered by where last/bid/ask is...
    * simply pass the order to the appropriate stop chain
    */
    plevel p = _ptoi(e.stop);
    stop_bndl bndl = stop_bndl(BuyStop, e.limit, e.id, e.sz, e.exec_cb);
    _chain<stop_chain_type>::push(this, p, std::move(bndl));
}


SOB_TEMPLATE
void 
SOB_CLASS::_clear_callback_queue()
{
    /* use _busy_with callbacks to abort recursive calls 
         if false, set to true(atomically) 
         if true leave it alone and return */  
    bool busy = false;
    _busy_with_callbacks.compare_exchange_strong(busy,true);
    if( busy ){ 
        return;
    }

    std::vector<dfrd_cb_elem> cb_elems;
    {     
        std::lock_guard<std::mutex> lock(_master_mtx); 
        /* --- CRITICAL SECTION --- */    
        std::move( _deferred_callbacks.begin(),
                   _deferred_callbacks.end(), 
                   back_inserter(cb_elems) );        
        _deferred_callbacks.clear(); 
        /* --- CRITICAL SECTION --- */
    }    
    
    for( const auto & e : cb_elems ){           
        if( e.exec_cb ){ 
            e.exec_cb( e.msg, e.id1, e.id2, e.price, e.sz );
        }
    }        
    _busy_with_callbacks.store(false);
}


/*
 *  CURRENTLY working under the constraint that stop priority goes:  
 *     low price to high for buys                                   
 *     high price to low for sells                                  
 *     buys before sells                                            
 *                                                                  
 *  (The other possibility is FIFO irrespective of price)              
 */

SOB_TEMPLATE
void 
SOB_CLASS::_look_for_triggered_stops()
{  /* 
    * PART OF THE ENCLOSING CRITICAL SECTION    
    *
    * we don't check against max/min, because of the cached high/lows 
    */
    assert(_last);
    for( plevel l = _low_buy_stop; l <= _last; ++l ){
        _handle_triggered_stop_chain<true>(l);
    }
    for( plevel h = _high_sell_stop; h>= _last; --h ){
        _handle_triggered_stop_chain<false>(h);
    }
    _need_check_for_stops = false;
}

// TODO how do we deal with IDs in the cache when stop is triggered???
SOB_TEMPLATE
template<bool BuyStops>
void 
SOB_CLASS::_handle_triggered_stop_chain(plevel plev)
{  /* 
    * PART OF THE ENCLOSING CRITICAL SECTION 
    */
    order_exec_cb_type cb;
    double limit;
    size_t sz;
    id_type id, id_new;
    /*
     * need to copy the relevant chain, delete original, THEN insert
     * if not we can hit the same order more than once / go into infinite loop
     */
    stop_chain_type cchain = std::move(plev->second);
    plev->second.clear();

    _stop_exec<BuyStops>::adjust_state_after_trigger(this, plev);

    for( auto & e : cchain ){
        id = e.id;
        limit = e.limit;
        cb = e.exec_cb;
        sz = e.sz;

        /* remove trailing stops (no need to check if is trailing stop) */
        _trailing_stop_erase(id, BuyStops);

        /* first we handle any (cancel) advanced conditions */
        if( _order::is_advanced(e) ){
            assert(e.trigger != condition_trigger::none);
            _handle_advanced_order_cancel(e, id);
        }
        
       /* UPDATE! we are creating new id for new exec_cb type (Jan 18) */
        id_new = _generate_id();

        if( cb ){
            callback_msg msg = limit ? callback_msg::stop_to_limit
                                     : callback_msg::stop_to_market;
            _push_callback(msg, cb, id, id_new, (limit ? limit : 0), sz);
        }

        order_type ot = limit ? order_type::limit : order_type::market;
        /*
         * we handle an advanced trigger condition after we push the contingent
         * market/limit, dropping the condition and trigger; except for
         * trailing stop and trailing bracket, which are transferred to the
         * new order so it be can constructed on execution, using that price
         */
        order_condition oc = e.cond;
        condition_trigger ct = e.trigger;
        std::unique_ptr<OrderParamaters> op = nullptr;
        std::unique_ptr<OrderParamaters> op2 = nullptr;
        if( _order::is_trailing_stop(e) ){
            assert( e.contingent_order->is_by_nticks() );
            op = std::unique_ptr<OrderParamaters>(
                    e.contingent_order->copy_new()
                    );
        }else if( _order::is_trailing_bracket(e) ){
            assert( e.nticks_bracket_orders->first.is_by_nticks() );
            assert( e.nticks_bracket_orders->second.is_by_nticks() );
            op = std::unique_ptr<OrderParamaters>(
                    e.nticks_bracket_orders->first.copy_new()
                    );
            op2 = std::unique_ptr<OrderParamaters>(
                    e.nticks_bracket_orders->second.copy_new()
                    );
        }else{
            oc = order_condition::none;
            ct = condition_trigger::none;
        }

        /*
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; we simply increment _noutstanding_orders instead
        * and block on that when necessary.
        */
        _push_order_no_wait( ot, e.is_buy, limit, 0, sz, cb, oc, ct,
                             std::move(op), std::move(op2), id_new );

        /* we need to trigger new orders AFTER we push the market/limit */
        if( _order::is_advanced(e)
            && !_order::is_trailing_stop(e)
            && !_order::is_trailing_bracket(e) )
        {
            assert(e.trigger != condition_trigger::none);
            _handle_advanced_order_trigger(e, id);
        }

        /* BUG FIX Feb 23 2018 - remove old ID from cache */
        _id_cache.erase(id);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_adjust_trailing_stops(bool buy_stops)
{
    auto& ids = buy_stops ? _trailing_buy_stops : _trailing_sell_stops;
    for( auto id : ids ){
        _adjust_trailing_stop(id, buy_stops);
    }
}


SOB_TEMPLATE
id_type
SOB_CLASS::_push_order_and_wait( order_type oty,
                                 bool buy,
                                 double limit,
                                 double stop,
                                 size_t size,
                                 order_exec_cb_type cb,
                                 order_condition cond,
                                 condition_trigger cond_trigger,
                                 std::unique_ptr<OrderParamaters>&& cparams1,
                                 std::unique_ptr<OrderParamaters>&& cparams2,
                                 id_type id )
{

    std::promise<id_type> p;
    std::future<id_type> f(p.get_future());
    _push_order(oty, buy, limit, stop, size, cb, cond, cond_trigger,
            std::move(cparams1), std::move(cparams2), id, std::move(p) );

    id_type ret_id;
    try{
        ret_id = f.get(); /* BLOCKING */
    }catch(...){
        _block_on_outstanding_orders(); /* BLOCKING */
        _clear_callback_queue();
        throw;
    }

    _block_on_outstanding_orders(); /* BLOCKING */
    _clear_callback_queue();
    return ret_id;
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order_no_wait( order_type oty,
                                bool buy,
                                double limit,
                                double stop,
                                size_t size,
                                order_exec_cb_type cb,
                                order_condition cond,
                                condition_trigger cond_trigger,
                                std::unique_ptr<OrderParamaters>&& cparams1,
                                std::unique_ptr<OrderParamaters>&& cparams2,
                                id_type id )
{
    _push_order( oty, buy, limit, stop, size, cb, cond, cond_trigger,
                std::move(cparams1), std::move(cparams2), id,
                /* dummy */ std::move( std::promise<id_type>() ) );
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order( order_type oty,
                        bool buy,
                        double limit,
                        double stop,
                        size_t size,
                        order_exec_cb_type cb,
                        order_condition cond,
                        condition_trigger cond_trigger,
                        std::unique_ptr<OrderParamaters>&& cparams1,
                        std::unique_ptr<OrderParamaters>&& cparams2,
                        id_type id,
                        std::promise<id_type>&& p )
{
    {
        std::lock_guard<std::mutex> lock(_order_queue_mtx);
        /* --- CRITICAL SECTION --- */
        _order_queue.push(
            { oty, buy, limit, stop, size, cb, cond, cond_trigger,
              std::move(cparams1), std::move(cparams2), id, std::move(p) }
        );
        ++_noutstanding_orders;
        /* --- CRITICAL SECTION --- */
    }
    _order_queue_cond.notify_one();
}


SOB_TEMPLATE
void 
SOB_CLASS::_block_on_outstanding_orders()
{
    while(1){
        {
            std::lock_guard<std::mutex> lock(_order_queue_mtx);
            /* --- CRITICAL SECTION --- */
            if( _noutstanding_orders < 0 ){
                throw std::runtime_error("_noutstanding_orders < 0");
            }else if( _noutstanding_orders == 0 ){
                break;
            }
            /* --- CRITICAL SECTION --- */
        }
        std::this_thread::yield();
    }
}


SOB_TEMPLATE
bool 
SOB_CLASS::_pull_order(id_type id, bool pull_linked)
{ 
    /* caller needs to hold lock on _master_mtx or race w/ callback queue */
    try{
        auto& cinfo = _id_cache.at(id);
        if( cinfo.first ){
            return _pull_order(id, cinfo.first, pull_linked, cinfo.second);
        }
    }catch(std::out_of_range&){
    }
    return false;
}


SOB_TEMPLATE
template<typename ChainTy>
bool 
SOB_CLASS::_pull_order(id_type id, plevel p, bool pull_linked)
{ 
    /* caller needs to hold lock on _master_mtx or race w/ callback queue */

    auto bndl = _chain<ChainTy>::pop(this, p, id);
    if( !bndl ){
        assert( _trailing_sell_stops.find(id) == _trailing_sell_stops.cend() );
        assert( _trailing_buy_stops.find(id) == _trailing_buy_stops.cend() );
        return false;
    }
    _push_callback(callback_msg::cancel, bndl.exec_cb, id, id, 0, 0);
    
    if( pull_linked ){
        _pull_linked_order<ChainTy>(bndl);
    }

    /* remove trailing stops (no need to check if is trailing stop) */
    if( _chain<ChainTy>::is_stop ){
        _trailing_stop_erase(id, _order::is_buy_stop(bndl));
    }
    return true;
}


SOB_TEMPLATE
template<typename ChainTy>
void
SOB_CLASS::_pull_linked_order(typename ChainTy::value_type& bndl)
{
    order_location *loc = bndl.linked_order;
    if( loc && _order::is_OCO(bndl) ){
        /* false to pull_linked; this side in process of being pulled */
        _pull_order(loc->id, loc->price, false, loc->is_limit_chain);
    }
}

SOB_TEMPLATE
template<typename ChainTy>
typename ChainTy::value_type&
SOB_CLASS::_find(id_type id) const
{
    try{
        plevel p = _ptoi( _id_cache.at(id).first );
        if( p ){
            return _order::template find<ChainTy>(p, id);
        }
    }catch(std::out_of_range&){
    }
    return ChainTy::value_type::null;
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy>
std::map<double, typename SOB_CLASS::template _depth<Side>::mapped_type>
SOB_CLASS::_market_depth(size_t depth) const
{
    plevel h;
    plevel l;    
    size_t d;
    std::map<double, typename _depth<Side>::mapped_type> md;
    
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */      
    _high_low<Side>::template get<ChainTy>(this,&h,&l,depth);
    for( ; h >= l; --h){
        if( h->first.empty() ){
            continue;
        }
        d = _chain<limit_chain_type>::size(&h->first);   
        auto v = _depth<Side>::build_value(this,h,d);
        md.insert( std::make_pair(_itop(h), v) );                 
    }
    return md;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy> 
size_t 
SOB_CLASS::_total_depth() const
{
    plevel h;
    plevel l;
    size_t tot = 0;
        
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */    
    _high_low<Side>::template get<ChainTy>(this,&h,&l);
    for( ; h >= l; --h){ 
        tot += _chain<ChainTy>::size( _chain<ChainTy>::get(h) );
    }
    return tot;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename ChainTy>
void
SOB_CLASS::_dump_orders(std::ostream& out,
                        plevel l,
                        plevel h,
                        side_of_trade sot) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    out << "*** (" << sot << ") " << _chain<ChainTy>::as_order_type()
        << "s ***" << std::endl;

    for( ; h >= l; --h){
        auto c = _chain<ChainTy>::get(h);
        if( !c->empty() ){
            out << _itop(h);
            for( const auto& e : *c ){
               _order::dump(out, e, _is_buy_order(h, e));
            }
            out << std::endl;
        }
    }
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
typename SOB_CLASS::plevel 
SOB_CLASS::_ptoi(TickPrice<TickRatio> price) const
{  /* 
    * the range check asserts in here are 1 position more restrictive to catch
    * bad user price data passed but allow internal index conversions(_itop)
    * 
    * this means that internally we should not convert to a price when
    * a pointer is past beg/at end, signaling a null value
    */   
    long long offset = (price - _base).as_ticks();
    plevel p = _beg + offset;
    assert(p >= _beg);
    assert(p <= (_end-1)); 
    return p;
}


SOB_TEMPLATE 
TickPrice<TickRatio>
SOB_CLASS::_itop(plevel p) const
{   
    _assert_plevel(p); // internal range and align check
    return _base + plevel_offset(p, _beg);
}


SOB_TEMPLATE
double
SOB_CLASS::_tick_price_or_throw(double price, std::string msg) const
{
    if( !is_valid_price(price) ){
        throw std::invalid_argument(msg);
    }
    return price_to_tick(price);
}


SOB_TEMPLATE
void
SOB_CLASS::_reset_internal_pointers( plevel old_beg,
                                     plevel new_beg,
                                     plevel old_end,
                                     plevel new_end,
                                     long long offset )
{   
    /*** PROTECTED BY _master_mtx ***/          
    if( _last ){
        _last = bytes_add(_last, offset);
    }

    /* if plevel is below _beg, it's empty and needs to follow new_beg */
    auto reset_low = [=](plevel *ptr){     
        *ptr = (*ptr == (old_beg-1))  ?  (new_beg - 1) : bytes_add(*ptr, offset);       
    };
    reset_low(&_bid);
    reset_low(&_high_sell_limit);
    reset_low(&_high_buy_stop);
    reset_low(&_high_sell_stop);
     
    /* if plevel is at _end, it's empty and needs to follow new_end */
    auto reset_high = [=](plevel *ptr){     
        *ptr = (*ptr == old_end)  ?  new_end : bytes_add(*ptr, offset);         
    };
    reset_high(&_ask);
    reset_high(&_low_buy_limit);
    reset_high(&_low_buy_stop);
    reset_high(&_low_sell_stop);
}


SOB_TEMPLATE
void
SOB_CLASS::_grow_book(TickPrice<TickRatio> min, size_t incr, bool at_beg)
{
    if( incr == 0 ){
        return;
    }

    plevel old_beg = _beg;
    plevel old_end = _end;
    size_t old_sz = _book.size();

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    /* after this point no guarantee about cached pointers */
    _book.insert( at_beg ? _book.begin() : _book.end(),
                  incr,
                  chain_pair_type() );
    /* the book is in an INVALID state until _reset_internal_pointers returns */

    _base = min;
    _beg = &(*_book.begin()) + 1;
    _end = &(*_book.end());

    long long offset = at_beg ? bytes_offset(_end, old_end)
                              : bytes_offset(_beg, old_beg);

    assert( equal( 
        bytes_offset(_end, _beg), 
        bytes_offset(old_end, old_beg) 
            + static_cast<long long>(sizeof(*_beg) * incr),  
        static_cast<long long>((old_sz + incr - 1) * sizeof(*_beg)),
        static_cast<long long>((_book.size() - 1) * sizeof(*_beg))
    ) );
    
    _reset_internal_pointers(old_beg, _beg, old_end, _end, offset);
    _assert_internal_pointers();    
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
void
SOB_CLASS::_assert_plevel(plevel p) const
{
    assert( (labs(bytes_offset(p, _beg)) % sizeof(chain_pair_type)) == 0 );
    assert( (labs(bytes_offset(p, _end)) % sizeof(chain_pair_type)) == 0 );
    assert( p >= (_beg - 1) );
    assert( p <= _end );
}


SOB_TEMPLATE
void
SOB_CLASS::_assert_internal_pointers() const
{
#ifndef NDEBUG  
    if( _last ){
        _assert_plevel(_last);
    }
    _assert_plevel(_bid);
    _assert_plevel(_ask);
    _assert_plevel(_low_buy_limit);
    _assert_plevel(_high_sell_limit);
    _assert_plevel(_low_buy_stop);
    _assert_plevel(_high_buy_stop);
    _assert_plevel(_low_sell_stop);
    _assert_plevel(_high_sell_stop);
    if( _bid != (_beg-1) ){
        if( _ask != _end ){
            assert( _ask > _bid );
        }
        if( _low_buy_limit != _end ){
            assert( _low_buy_limit <= _bid);
        }
    }
    if( _high_sell_limit != (_beg-1) ){
        if( _ask != _end ){
            assert( _high_sell_limit >= _ask );
        }
        if( _low_buy_limit != _end ){
            assert( _high_sell_limit > _low_buy_limit );
        }
    }
    if( _low_buy_stop != _end || _high_buy_stop != (_beg-1) ){  // OR
        assert( _high_buy_stop >= _low_buy_stop );
    }
    if( _low_sell_stop != _end || _high_sell_stop != (_beg-1) ){ // OR
        assert( _high_sell_stop >= _low_sell_stop );
    }
#endif
}

}; /* sob */

#undef SOB_TEMPLATE
#undef SOB_CLASS
