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
#include "../../include/order_util.hpp"
#include "specials.tpp"

#define SOB_CLASS SimpleOrderbook::SimpleOrderbookBase

// NOTE - only explicitly instantiate members needed for link and not
//        done implicitly. If (later) called from outside core.cpp
//        need to add them.

// TODO pre-post AON checks in market insert
// TODO price-mediation in gapped fills (user input ?)
// TODO cache the aon levels in a list to avoid gaps in 'window'
//

namespace sob{


/***************************************************************
              *** our ersatz iterator approach ****
                        i = [ 0, incr )

 vector iter    [begin()]                               [ end() ]
 internal pntr  [ _base ][ _beg ]           [ _end - 1 ][ _end  ]
 internal index [ NULL  ][   i  ][ i+1 ]...   [ incr-1 ][  NULL ]
 external price [ THROW ][ min  ]              [  max  ][ THROW ]
*****************************************************************/
SOB_CLASS::SimpleOrderbookBase(size_t incr, itop_ty itop, ptoi_ty ptoi)
    :
        /* actual orderbook object */
        _book(incr + 1), /*pad the beg side */
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
        _low_buy_aon( _end ),
        _high_buy_aon( _beg - 1 ),
        _low_sell_aon( _end ),
        _high_sell_aon( _beg - 1 ),
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
        _master_run_flag(true),
        /* price <-> tick conversion functions */
        _itop(itop),
        _ptoi(ptoi)
    {
        /*** DONT THROW AFTER THIS POINT ***/
        _order_dispatcher_thread =
            std::thread(std::bind(&SOB_CLASS::_threaded_order_dispatcher,this));
    }


SOB_CLASS::~SimpleOrderbookBase()
    {
        _master_run_flag = false;
        try{
            {
                std::lock_guard<std::mutex> lock(_order_queue_mtx);
                _order_queue.emplace();
                /* don't incr _noutstanding_orders;
                   we break main loop before we can decr */
            }
            _order_queue_cond.notify_one();
            if( _order_dispatcher_thread.joinable() ){
                _order_dispatcher_thread.join();
            }
        }catch(...){
            std::cerr<< "exception in sob destructor" << std::endl;
        }
    }


void
SOB_CLASS::_threaded_order_dispatcher()
{
    for( ; ; ){
        order_queue_elem e; // dangling internals?
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


bool
SOB_CLASS::_insert_order(const order_queue_elem& e)
{
    if( detail::order::is_advanced(e) ){
        _route_advanced_order(e);
        return true;
    }
    if( detail::order::is_null(e) ){
        /* not the cleanest but most effective/thread-safe
           success/fail is returned in the in e.id*/
        return _pull_order(e.id, true);
    }
    _route_basic_order<>(e);
    return true;
}


template<side_of_trade side>
fill_type
SOB_CLASS::_route_basic_order(const order_queue_elem& e, bool pass_conditions)
{
    constexpr bool IsBuy = side == side_of_trade::buy;

    if( side == side_of_trade::both ){
        return e.is_buy
            ? _route_basic_order<side_of_trade::buy>(e, pass_conditions)
            : _route_basic_order<side_of_trade::sell>(e, pass_conditions);
    }

    switch( e.type ){
    case order_type::limit:
        return _insert_limit_order<IsBuy>(e,pass_conditions);
    case order_type::market:
        _insert_market_order<IsBuy>(e);
        return fill_type::immediate_full;
    case order_type::stop: /* no break */
    case order_type::stop_limit:
        _insert_stop_order<IsBuy>(e, pass_conditions);
        return fill_type::none;
    default:
        throw std::runtime_error("invalid order type in order_queue");
    }
}

template fill_type
SOB_CLASS::_route_basic_order<side_of_trade::both>(const order_queue_elem&, bool);

template fill_type
SOB_CLASS::_route_basic_order<side_of_trade::buy>(const order_queue_elem&, bool);

template fill_type
SOB_CLASS::_route_basic_order<side_of_trade::sell>(const order_queue_elem&, bool);



/*
 *  the guts of order execution:
 *    - match orders against the order book,
 *    - adjust internal state,
 *    - check for overflows
 *    - if price changed adjust trailing stops
 *    - look/execute stop orders
 */
template<bool BidSide>
size_t
SOB_CLASS::_trade( plevel plev,
                   id_type id,
                   size_t size,
                   const order_exec_cb_type& exec_cb )
{
    using namespace detail;
    using AON = exec::aon<BidSide>;
    using CORE = exec::core<BidSide>;
    using LIMIT = exec::limit<BidSide>;

    assert(plev);

    bool all;
    plevel old_last = _last;
    plevel p = CORE::begin(this);
    plevel end = CORE::end(this); // ALL orders must go thru queue

    while( size
           && CORE::inside_of(p, plev)
           && CORE::inside_of(p, end) )
    {
        if( AON::in_window(this, p) ){
            /* first match against the AON chain */
            aon_chain_type *ac = p->get_aon_chain<BidSide>();
            if( ac ){
                std::tie(size, all) = _hit_aon_chain(ac, p, id, size, exec_cb);
                if( all ){
                    p->destroy_aon_chain<BidSide>();
                    AON::adjust_state_after_pull(this, p);
                }
            }
        }

        if( LIMIT::in_window(this, p) ){
            /* then match against the limit chain (which CAN have AON orders) */
            std::tie(size, all) = _hit_chain( p, id, size, exec_cb );
            if( all )
                CORE::find_new_best_inside(this);
        }

        p = CORE::next_or_jump(this, p);
    }

    /*
     * dont need to check that old != 0; in order to have an active
     * trailing_stop we must have had an initial trade
     */
    if( old_last != _last )
        _adjust_trailing_stops(_last < old_last);

    if( _need_check_for_stops )
        _look_for_triggered_stops();

    return size; /* what we couldn't fill */
}


/*
 *  handles all trades against the limit chain at a particular plevel; a
 *  limit chain can hold a limit_bndl or aon_bndl AFTER the first order(
 *  which can only be a limit chain )
 */
std::pair<size_t, bool>
SOB_CLASS::_hit_chain( plevel plev,
                       id_type id,
                       size_t size,
                       const order_exec_cb_type& exec_cb )
{
    using namespace detail;

    limit_chain_type *pchain = plev->get_limit_chain();

    auto pos = pchain->begin();
    for( ; pos != pchain->end() && size > 0; ++pos )
    {
        /* if AON need to make sure enough size  */
        if( order::is_AON(*pos) ){
            if( size < pos->sz ){ /* if not, move to aon chain */
                chain<limit_chain_type>::copy_bndl_to_aon_chain(this, plev, pos);
                pos->sz = 0; // signal erase if last
                continue;
            }
        }

        size_t amount = std::min(size, pos->sz);

        /* push callbacks onto queue; update state */
        _trade_has_occured( plev, amount, id, pos->id, exec_cb, pos->exec_cb );

        /* reduce the amount left to trade */
        size -= amount;     

        /* deal with advanced order conditions */
        if( order::is_advanced(*pos) && order::has_condition_trigger(*pos) )
        {
            /* check if trigger condition is met */
            if( !order::needs_full_fill(*pos) || (pos->sz == amount) )
            {
                // note - pos->sz hasn't been updated yet!
                _handle_advanced_order_cancel(*pos, pos->id)
                    || _handle_advanced_order_trigger(*pos, pos->id);
            }
        }

        /* remaining (adjust after we handle advanced conditions) */
        pos->sz -= amount;

        /* remove from cache if none left */
        if( pos->sz == 0 )
            _id_cache.erase(pos->id);
    }

    /* backup to see if last order was completely filled, if so re-incr */
    --pos;
    auto r = pchain->erase( pchain->begin(), (pos->sz ? pos : ++pos) );
    return std::make_pair(size, r == pchain->end());
}


/*
 *  handles all trades against the aon chain at a particular plevel; an aon
 *  chain only holds aon_bndls, all of which are older than orders on the
 *  corresponding limit chain and therefore matched first
 */
std::pair<size_t, bool>
SOB_CLASS::_hit_aon_chain( aon_chain_type *achain,
                           plevel plev,
                           id_type id,
                           size_t size,
                           const order_exec_cb_type& exec_cb )
{
    assert( !achain->empty() );
    for( auto pos = achain->begin(); pos != achain->end() && size > 0; )
    {
        if( size >= pos->sz ){
            _trade_has_occured(plev, pos->sz, id, pos->id, exec_cb, pos->exec_cb);
            size -= pos->sz;
            _id_cache.erase(pos->id);
            pos = achain->erase(pos);
        }else
            ++pos;
    }
    return std::make_pair(size, achain->empty());
}


/* two orders have been matched */
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
    _deferred_callbacks.emplace_back(
        callback_msg::fill, cbbuy, idbuy, idbuy, p, size
        );
    _deferred_callbacks.emplace_back(
        callback_msg::fill, cbsell, idsell, idsell, p, size
        );

    _timesales.push_back( std::make_tuple(clock_type::now(), p, size) );
    _last = plev;
    _total_volume += size;
    _last_size = size;
    _need_check_for_stops = true;
}


/*
 * CHECK IF ANY OLD AONs can be filled against new limit BEFORE we
 * send it to be matched against the book
 *
 *   An aon @ a better price should take priority over any standard
 *   limits on the other side.
 *
 *   AONs that are farthest from the limit are executed first.
 */
template<bool IsBuy>
size_t
SOB_CLASS::_match_aon_orders_PRE_trade(const order_queue_elem& e, plevel p)
{
    using namespace detail;

    assert(p);
    size_t rmndr = e.sz;
    bool is_aon = order::is_AON(e);
    auto overlaps = exec::aon<!IsBuy>::overlapping(this, p);

    for( auto a = overlaps.rbegin(); a != overlaps.rend(); ++a )
    {
        plevel paon = a->first;
        aon_bndl& aon = a->second.get();
        auto fillable = _limit_is_fillable<!IsBuy>(paon, aon.sz, false);
        size_t available = rmndr + fillable.second;

        if( fillable.first
            || (is_aon && (aon.sz == available))
            || (!is_aon && (aon.sz < available)) )
        {
            auto bndl = chain<aon_chain_type>::pop(this, aon.id);

            size_t r = _trade<IsBuy>(paon, bndl.id, bndl.sz, bndl.exec_cb);
            if( r > rmndr )
                throw std::runtime_error("AON has left over size(PRE)");

            size_t filled_this = std::min(r, rmndr);
            if( filled_this > 0 ){
                /*
                 * make the trade w/ *this* limit
                 * right now just use *this* plevel for price, in the future
                 * we'll want a more robust price-mediation mechanism
                 */
                _trade_has_occured( p , filled_this, bndl.id, e.id,
                                    bndl.exec_cb, e.exec_cb );

                // our limit still has this much left
                rmndr -= filled_this;
            }
            if( rmndr == 0 )
                break;
        }
    }
    return rmndr;
}


/*
 * CHECK IF ANY OLD AONs can be filled against new limit AFTER its been
 * matched against the book
 *
 *   If we've inserted a new limit we now have to check if we can fill
 *   any old(er) AONs that overlap with that plevel (or better)
 *
 *   AONs that are farthest from the limit are executed first.
 */
template<bool IsBuy>
void
SOB_CLASS::_match_aon_orders_POST_trade(const order_queue_elem& e, plevel p)
{
    using namespace detail;

    auto overlaps = exec::aon<!IsBuy>::overlapping(this, p);

    for( auto a = overlaps.rbegin(); a != overlaps.rend(); ++a ){
        plevel p = a->first;
        aon_bndl& aon = a->second.get();
        if( _limit_is_fillable<!IsBuy>(p, aon.sz, false).first )
        {
            auto bndl = chain<aon_chain_type>::pop(this, aon.id);
            if( _trade<IsBuy>(p, bndl.id, bndl.sz, bndl.exec_cb) )
            {
                throw std::runtime_error("AON has left over size(POST)");
            }
        }
    }
}


template<bool BuyLimit>
sob::fill_type
SOB_CLASS::_insert_limit_order(const order_queue_elem& e, bool pass_conditions)
{
    using namespace detail;

    assert( order::is_limit(e) );
    plevel p = _ptoi(e.limit);

    /* execute any AONs that are valid w/ this order now available */
    size_t rmndr = _match_aon_orders_PRE_trade<BuyLimit>(e, p);
    if( rmndr == 0)
        return fill_type::immediate_full;

    /* If there are matching orders on the other side ... */
    if( exec::core<!BuyLimit>::is_tradable(this, p) ){
        /*
         * if dealing with AON order we need to 'look-ahead' for a possible
         * full fill before we send to the matching engine
         */
        if( !order::is_AON(e)
            || _limit_is_fillable<BuyLimit>(p, rmndr, false).first )
        {
            // pass ref to callback functor, we'll copy later if necessary
            // return what we couldn't fill
            rmndr = _trade<!BuyLimit>(p, e.id, rmndr, e.exec_cb);
        }
    }

    if( rmndr == 0)
        return fill_type::immediate_full;

    if( order::is_AON(e)
        && ( exec::limit<!BuyLimit>::is_tradable(this,p)
             || p->limit_chain_is_empty() ) )
    {
        /*
        *  if 'is_tradable' we have an opposing order at this plevel so
        *  we NEED to go straight to the aon chain (note - is_tradable
        *  assumes a non-aon order so we really can't execute)
        *
        *  if not, and we'd be the first order on this limit chain,
        *  we also NEED to go straight to the AON chain
        */
        chain<aon_chain_type>::template push<BuyLimit>( this, p,
            pass_conditions
                ? aon_bndl(e.id, rmndr, e.exec_cb, e.cond, e.cond_trigger)
                : aon_bndl(e.id, rmndr, e.exec_cb)
        );
    }
    else
    {
        assert( !exec::limit<!BuyLimit>::is_tradable(this,p) );

        chain<limit_chain_type>::template push<BuyLimit>( this, p,
            pass_conditions
                ? limit_bndl(e.id, rmndr, e.exec_cb, e.cond, e.cond_trigger)
                : limit_bndl(e.id, rmndr, e.exec_cb)
        );
    }

    /* execute any AONs that are valid w/ the remainder of this order */
    _match_aon_orders_POST_trade<BuyLimit>(e, p);

    /* we don't know if order filled via aon overlap check so look in cache */
    try{
        auto& iwrap = _from_cache(e.id);
        assert( !iwrap.is_stop() );
        if( iwrap->sz != e.sz )
            return fill_type::immediate_partial;
    }catch( OrderNotInCache& e ){
        return fill_type::immediate_full;
    }

    /* we're still in the cache AND have the original size so... */
    return fill_type::none;
}


template<bool BuyMarket>
void
SOB_CLASS::_insert_market_order(const order_queue_elem& e)
{
    assert( detail::order::is_market(e) );

    /* expects a valid plevel so simulate one */
    plevel p = BuyMarket ? (_end - 1) : _beg;

    size_t rmndr = _match_aon_orders_PRE_trade<BuyMarket>(e, p);
    if( rmndr )
        rmndr = _trade<!BuyMarket>(p, e.id, rmndr, e.exec_cb);

    if( rmndr > 0 ){
        throw liquidity_exception( e.sz, rmndr, e.id);
    }
}


template<bool BuyStop>
void
SOB_CLASS::_insert_stop_order(const order_queue_elem& e, bool pass_conditions)
{
    assert( detail::order::is_stop(e) );
   /*
    * we need an actual trade @/through the stop, i.e can't assume
    * it's already been triggered by where last/bid/ask is...
    * simply pass the order to the appropriate stop chain
    */
    detail::chain<stop_chain_type>::push(
        this,
        _ptoi(e.stop),
        pass_conditions
            ? stop_bndl(BuyStop, e.limit, e.id, e.sz, e.exec_cb, e.cond,
                        e.cond_trigger)
            : stop_bndl(BuyStop, e.limit, e.id, e.sz, e.exec_cb)
        );
}


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
        if( e.exec_cb )
            e.exec_cb( e.msg, e.id1, e.id2, e.price, e.sz );
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
void
SOB_CLASS::_look_for_triggered_stops()
{  /*
    * PART OF THE ENCLOSING CRITICAL SECTION
    *
    * we don't check against max/min, because of the cached high/lows
    */
    assert(_last);
    plevel p;

    for( p = _low_buy_stop; p <= _last; ++p )
        _handle_triggered_stop_chain<true>(p);

    for( p = _high_sell_stop; p >= _last; --p )
        _handle_triggered_stop_chain<false>(p);

    _need_check_for_stops = false;
}


// TODO how do we deal with IDs in the cache when stop is triggered???
// TODO optimize this (major bottleneck)
template<bool BuyStops>
void
SOB_CLASS::_handle_triggered_stop_chain(plevel plev)
{  /*
    * PART OF THE ENCLOSING CRITICAL SECTION
    */
    using namespace detail;

    order_exec_cb_type cb;
    double limit;
    size_t sz;
    id_type id, id_new;

    /*
     * need to copy the relevant chain, delete original, THEN insert
     * if not we can hit the same order more than once / go into infinite loop
     *
     * TODO this move is still somewhat expensive, consider other approaches
     */
    stop_chain_type *pchain = plev->get_stop_chain();
    stop_chain_type cchain = std::move( *pchain );
    pchain->clear();

    exec::stop<BuyStops>::adjust_state_after_trigger(this, plev);

    for( auto & e : cchain ){
        id = e.id;
        limit = e.limit;
        cb = e.exec_cb;
        sz = e.sz;

        /* remove trailing stops (no need to check if is trailing stop) */
        _trailing_stop_erase(id, BuyStops);

        /* first we handle any (cancel) advanced conditions */
        if( order::is_advanced(e) ){
            assert(e.trigger != condition_trigger::none);
            _handle_advanced_order_cancel(e, id);
        }

       /* UPDATE! we are creating new id for new exec_cb type (Jan 18) */
        id_new = _generate_id();

        if( cb ){
            callback_msg msg = limit ? callback_msg::stop_to_limit
                                     : callback_msg::stop_to_market;
            _deferred_callbacks.emplace_back(msg, cb, id, id_new, limit, sz);
        }

        order_type ot = limit ? order_type::limit : order_type::market;

        /*
         * we can't use the blocking version of _push_order or we'll deadlock
         * the order_queue; we simply increment _noutstanding_orders instead
         * and block on that when necessary.
         */
        if( detail::order::is_trailing_stop(e) )
        {
            assert( e.contingent_order->is_by_nticks() );
            _push_order_no_wait( ot, e.is_buy, limit, 0, sz, cb, e.cond,
                                 e.trigger, e.contingent_order->copy_new(),
                                 nullptr, id_new );
        }
        else if( detail::order::is_trailing_bracket(e) )
        {
            assert( e.nticks_bracket_orders->first.is_by_nticks() );
            assert( e.nticks_bracket_orders->second.is_by_nticks() );
            _push_order_no_wait( ot, e.is_buy, limit, 0, sz, cb, e.cond,
                                 e.trigger,
                                 e.nticks_bracket_orders->first.copy_new(),
                                 e.nticks_bracket_orders->second.copy_new(),
                                 id_new );
        }
        else
        {
            _push_order_no_wait( ot, e.is_buy, limit, 0, sz, cb,
                                 order_condition::none, condition_trigger::none,
                                 nullptr, nullptr, id_new );
        }

        /*
         * we handle an advanced trigger condition AFTER we push the contingent
         * market/limit, dropping the condition and trigger; except for
         * trailing stop and trailing bracket, which are transferred to the
         * new order so it be can constructed on execution, using that price
         */
        if( order::is_advanced(e)
            && !order::is_trailing_stop(e)
            && !order::is_trailing_bracket(e) )
        {
            assert(e.trigger != condition_trigger::none);
            _handle_advanced_order_trigger(e, id);
        }

        /* BUG FIX Feb 23 2018 - remove old ID from cache */
        _id_cache.erase(id);
    }
}


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
                 /*dummy*/ std::promise<id_type>() );
}


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
        _order_queue.push( // faster than emplace + cnstr
             { oty, buy, limit, stop, size, cb, cond, cond_trigger,
               std::move(cparams1), std::move(cparams2), id, std::move(p) }
        );
        ++_noutstanding_orders;
        /* --- CRITICAL SECTION --- */
    }
    _order_queue_cond.notify_one();
}


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

// note this only returns the total fillable until we conclude the limit is
// fillable (if not it returns everything available)
template<bool IsBuy>
std::pair<bool, size_t>
SOB_CLASS::_limit_is_fillable( plevel p, size_t sz, bool allow_partial )
{
    using namespace detail;
    using CORE = exec::core<!IsBuy>;
    using LIMIT = exec::limit<!IsBuy>;

    size_t tot = 0;
    auto check_elem = [&](size_t elem_sz, bool is_aon){
        if( is_aon ){
            if( (sz-tot) >= elem_sz){
                tot += elem_sz;
                if( allow_partial )
                    return true;
            }
        }else{
            tot += elem_sz;
            if( allow_partial )
                return true;
        }
        if( tot >= sz ){
            assert( !is_aon || (tot == sz) );
            return true;
        }
        return false;
    };

    ;

    for( auto b = CORE::begin(this); CORE::inside_of(b,p); b = CORE::next(b) )
    {
        // first check the aon order chain at this plevel
        auto *ac = chain<aon_chain_type>::get<!IsBuy>(b);
        if( ac ){
            assert( !ac->empty() );
            for( auto& elem : *ac ){
                if( check_elem( elem.sz, true ) )
                    return {true, tot} ;
            }
        }
        // then check the limit chain, if tradable (can have aons as well)
        if( LIMIT::in_window(this,b) ){
            auto *lc = chain<limit_chain_type>::get(b);
            for( auto& elem : *lc ) {
                if( check_elem( elem.sz, order::is_AON(elem)) )
                    return {true, tot};
            }
        }
    }

    return {false, tot};
}

template std::pair<bool, size_t>
SOB_CLASS::_limit_is_fillable<true>(plevel, size_t, bool);

template std::pair<bool, size_t>
SOB_CLASS::_limit_is_fillable<false>(plevel, size_t, bool);


SOB_CLASS::chain_iter_wrap&
SOB_CLASS::_from_cache(id_type id)
{
    auto elem = _id_cache.find(id);
    if( elem == _id_cache.end() )
        throw OrderNotInCache(id);
    return elem->second;
}

const SOB_CLASS::chain_iter_wrap&
SOB_CLASS::_from_cache(id_type id) const
{
    auto elem = _id_cache.find(id);
    if( elem == _id_cache.end() )
        throw OrderNotInCache(id);
    return elem->second;
}


bool
SOB_CLASS::_pull_order(id_type id, bool pull_linked)
{
    /* caller needs to hold lock on _master_mtx or race w/ callback queue */
    try{
        const auto& iwrap = _from_cache(id);
        switch( iwrap.type ){
        case chain_iter_wrap::itype::limit:
            return _pull_order<limit_chain_type>(id, pull_linked);
        case chain_iter_wrap::itype::stop:
            return _pull_order<stop_chain_type>(id, pull_linked);
        case chain_iter_wrap::itype::aon_buy:
        case chain_iter_wrap::itype::aon_sell:
            return _pull_order<aon_chain_type>(id, false);
        }
    }catch( OrderNotInCache& e )
    {}
    return false;
}


template<typename ChainTy>
bool
SOB_CLASS::_pull_order(id_type id, bool pull_linked)
{
    /* caller needs to hold lock on _master_mtx or race w/ callback queue */

    using namespace detail;
    try{
        auto bndl = chain<ChainTy>::pop(this, id);
        if( !bndl )
            return false;

        _deferred_callbacks.emplace_back(
            callback_msg::cancel, bndl.exec_cb, id, id, 0, 0
            );

        if( pull_linked )
            _pull_linked_order<ChainTy>(bndl);

        /* remove trailing stops (no need to check if is trailing stop) */
        if( chain<ChainTy>::is_stop )
            _trailing_stop_erase(id, order::is_buy_stop(bndl));

    }catch( OrderNotInCache& e ){
        return false;
    }

    return true;
}
template bool SOB_CLASS::_pull_order<SOB_CLASS::limit_chain_type>(id_type,  bool);
template bool SOB_CLASS::_pull_order<SOB_CLASS::stop_chain_type>(id_type, bool);


template<typename ChainTy>
void
SOB_CLASS::_pull_linked_order(typename ChainTy::value_type& bndl)
{
    order_location *loc = bndl.linked_order;
    if( loc && detail::order::is_OCO(bndl) ){
        /* false to pull_linked; this side in process of being pulled */
        if( loc->is_limit_chain )
            _pull_order<limit_chain_type>(loc->id, false);
        else
            _pull_order<stop_chain_type>(loc->id, false);
    }
}


bool
SOB_CLASS::_in_cache(id_type id) const
{
    return _id_cache.count(id);
}

bool
SOB_CLASS::_is_buy_order(plevel p, const stop_bndl& o) const
{ return detail::order::is_buy_stop(o); }

bool
SOB_CLASS::_is_buy_order(plevel p, const limit_bndl& o) const
{ return (p < _ask); }


double
SOB_CLASS::_tick_price_or_throw(double price, std::string msg) const
{
    if( !is_valid_price(price) ){
        throw std::invalid_argument(msg);
    }
    return price_to_tick(price);
}


void
SOB_CLASS::_reset_internal_pointers( plevel old_beg,
                                     plevel new_beg,
                                     plevel old_end,
                                     plevel new_end,
                                     long long offset )
{
    /*** PROTECTED BY _master_mtx ***/
    if( _last )
        _last = bytes_add(_last, offset);    

    /* if plevel is below _beg, it's empty and needs to follow new_beg */
    auto reset_low = [=](plevel *ptr){
        *ptr = (*ptr == (old_beg-1))  ?  (new_beg - 1) : bytes_add(*ptr, offset);
    };
    reset_low(&_bid);
    reset_low(&_high_sell_limit);
    reset_low(&_high_buy_stop);
    reset_low(&_high_sell_stop);
    reset_low(&_high_buy_aon);
    reset_low(&_high_sell_aon);

    /* if plevel is at _end, it's empty and needs to follow new_end */
    auto reset_high = [=](plevel *ptr){
        *ptr = (*ptr == old_end)  ?  new_end : bytes_add(*ptr, offset);
    };
    reset_high(&_ask);
    reset_high(&_low_buy_limit);
    reset_high(&_low_buy_stop);
    reset_high(&_low_sell_stop);
    reset_high(&_low_buy_aon);
    reset_high(&_low_sell_aon);

    /* adjust the cache elems (BUG FIX Apr 25 2019) */
    for( auto& elem : _id_cache )
        elem.second.p = bytes_add(elem.second.p, offset);
}


void
SOB_CLASS::_assert_plevel(plevel p) const
{
#ifndef NDEBUG
    const level *b = &(*_book.cbegin());
    const level *e = &(*_book.cend());
    assert( (labs(bytes_offset(p, b)) % sizeof(level)) == 0 );
    assert( (labs(bytes_offset(p, e)) % sizeof(level)) == 0 );
    assert( p >= b );
    assert( p <= e );
#endif
}


void
SOB_CLASS::_assert_internal_pointers() const
{
#ifndef NDEBUG
    if( _last )
        _assert_plevel(_last);
    
    assert( _beg == &(*_book.begin()) + 1 );
    assert( _end == &(*_book.end()) );
    _assert_plevel(_bid);
    _assert_plevel(_ask);
    _assert_plevel(_low_buy_limit);
    _assert_plevel(_high_sell_limit);
    _assert_plevel(_low_buy_stop);
    _assert_plevel(_high_buy_stop);
    _assert_plevel(_low_sell_stop);
    _assert_plevel(_high_sell_stop);

    if( _bid != (_beg-1) ){
        if( _ask != _end )
            assert( _ask > _bid );        
        if( _low_buy_limit != _end )
            assert( _low_buy_limit <= _bid);        
    }

    if( _high_sell_limit != (_beg-1) ){
        if( _ask != _end )
            assert( _high_sell_limit >= _ask );        
        if( _low_buy_limit != _end )
            assert( _high_sell_limit > _low_buy_limit );        
    }

    if( _low_buy_stop != _end || _high_buy_stop != (_beg-1) )
    {  
        assert( _high_buy_stop >= _low_buy_stop );
    }

    if( _low_sell_stop != _end || _high_sell_stop != (_beg-1) )
    {
        assert( _high_sell_stop >= _low_sell_stop );
    }
#endif
}

}; /* sob */

#undef SOB_CLASS
