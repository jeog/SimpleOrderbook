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
#include "../include/simpleorderbook.hpp"

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
                /* don't incr _noutstanding_orders; we break main loop before we can decr */
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
        id_type id = e.id; // copy
        if( !id ){
            id = _generate_id();
        }
        assert(id > 0);

        try{
            std::lock_guard<std::mutex> lock(_master_mtx);
            /* --- CRITICAL SECTION --- */
            e.id = id;
            if( !_insert_order(e) ){
                id = 0; // bad pull
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
        p.set_value(id);
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_insert_order(order_queue_elem& e)
{
    if( e.cond != order_condition::none ){
        _route_advanced_order(e);
        return true;
    }
    if( e.type == order_type::null ){
        /* not the cleanest but most effective/thread-safe
           e.is_buy indicates to check limits first (not buy/sell)
           success/fail is returned in the in e.id*/
        return _pull_order(e.id, true, e.is_buy);
    }
    _route_basic_order(e);
    return true;
}


SOB_TEMPLATE
fill_type
SOB_CLASS::_route_basic_order(order_queue_elem& e)
{
    switch( e.type ){
    case order_type::limit:
        return  e.is_buy
            ? _insert_limit_order<true>(_ptoi(e.limit), e.sz, e.exec_cb, e.id)
            : _insert_limit_order<false>(_ptoi(e.limit), e.sz, e.exec_cb, e.id);

    case order_type::market:
        e.is_buy
            ? _insert_market_order<true>(e.sz, e.exec_cb, e.id)
            : _insert_market_order<false>(e.sz, e.exec_cb, e.id);
        return fill_type::immediate_full;

    case order_type::stop: /* no break */
    case order_type::stop_limit:
        e.is_buy
            ? _insert_stop_order<true>(_ptoi(e.stop), e.limit, e.sz, e.exec_cb, e.id)
            : _insert_stop_order<false>(_ptoi(e.stop), e.limit, e.sz, e.exec_cb, e.id);
        return fill_type::none;

    default:
        throw std::runtime_error("invalid order type in order_queue");
    }
}

// TODO figure out how to handle race condition concerning callbacks and
//      ID changes when an advanced order fills immediately(upon _inject)
//      and we callback with new ID BEFORE the user gets the original ID
//      from the initial call !
SOB_TEMPLATE
void
SOB_CLASS::_route_advanced_order(order_queue_elem& e)
{
    switch(e.cond){
    case order_condition::_bracket_active: /* no break */
    case order_condition::one_cancels_other:
        _insert_OCO_order(e);
        break;
    case order_condition::trailing_stop: /* no break */
    case order_condition::bracket: /* no break */
    case order_condition::one_triggers_other:
        _insert_OTO_order(e);
        break;
    case order_condition::fill_or_kill:
        _insert_FOK_order(e);
        break;
    case order_condition::_trailing_stop_active:
        _insert_trailing_stop_order(e);
        break;
    default:
        throw std::runtime_error("invalid advanced order condition");
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_inject_order(order_queue_elem& e, bool partial_ok)
{
    switch( _route_basic_order(e) ){
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
                   order_exec_cb_type& exec_cb )
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
                       order_exec_cb_type& exec_cb )
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
            if( elem.trigger != condition_trigger::fill_full || !rmndr ){
                _handle_advanced_order_cancel(elem, elem.id)
                || _handle_advanced_order_trigger(elem, elem.id);
            }
        }

        /* adjust outstanding order size or indicate removal if none left */
        if( rmndr > 0 ){ 
            elem.sz = rmndr;
        }else{                    
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
                               order_exec_cb_type& cbbuy,
                               order_exec_cb_type& cbsell )
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
bool
SOB_CLASS::_handle_advanced_order_trigger(_order_bndl& bndl, id_type id)
{
    switch(bndl.cond){
    case order_condition::one_cancels_other:
    case order_condition::_bracket_active:
    case order_condition::_trailing_stop_active:
        return false; /* NO OP */
    case order_condition::one_triggers_other:
    case order_condition::bracket:
    case order_condition::trailing_stop:
         _handle_OTO(bndl, id);
        return true;
    default:
        throw std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_handle_advanced_order_cancel(_order_bndl& bndl, id_type id)
{
    switch(bndl.cond){
    case order_condition::one_triggers_other:
    case order_condition::bracket:
    case order_condition::trailing_stop:
    case order_condition::_trailing_stop_active:
        return false; /* NO OP */
    case order_condition::one_cancels_other:
    case order_condition::_bracket_active:
        _handle_OCO(bndl, id);
        return true;
    default:
        throw std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OCO(_order_bndl& bndl, id_type id)
{
    order_location *loc = bndl.linked_order;
    assert(loc);
    assert( _order::is_OCO(bndl) || _order::is_active_bracket(bndl) );

    /* issue callback with new order # */
    id_type id_old = loc->is_primary ? loc->id : id;
    callback_msg msg = _order::is_OCO(bndl)
                     ? callback_msg::trigger_OCO
                     : callback_msg::trigger_BRACKET_close;
    _push_callback(msg, bndl.exec_cb, id_old, id, 0, 0);

    /* remove primary order, BE SURE pull_linked=false */
    _pull_order(loc->id, loc->price, false, loc->is_limit_chain);

    /* remove linked order from union */
    delete loc;
    bndl.linked_order = nullptr;
    bndl.cond = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OTO(_order_bndl& bndl, id_type id)
{
    // TODO avoid the copies
    OrderParamaters cparams1;
    OrderParamaters cparams2;

    id_type id_new = _generate_id();
    switch( bndl.cond ){
    case order_condition::bracket:
        /*
         * if triggered order is bracket we need to push order onto queue
         * FOR FAIRNESS
         *    1) use second order that bndl.bracket_orders points at,
         *       which makes the 'target' order primary
         *    2) the first order (stop/loss) is then used for cparams1
         *    3) change the condition to _bracket_active (basically an OCO)
         *    4) keep trigger_condition the same
         *    5) use the new id
         * when done delete what bracket_orders points at; we need to do this
         * because now the order is of condition _bracket_active, which needs
         * to be treated as an OCO, and use linked_order point in the anonymous
         * union. (could be an issue w/ how we move/delete union!)
         */
        _push_callback(callback_msg::trigger_BRACKET_open, bndl.exec_cb,
                       id, id_new, 0, 0);

        assert(bndl.bracket_orders);
        cparams1 = bndl.bracket_orders->first;
        cparams2 = bndl.bracket_orders->second;
        assert( cparams1.is_stop_order() );
        assert( cparams2.is_limit_order() );

        _push_order_no_wait( cparams2.get_order_type(), cparams2.is_buy(),
                             cparams2.limit(), cparams2.stop(), cparams2.size(),
                             bndl.exec_cb, order_condition::_bracket_active,
                             bndl.trigger,
                             std::unique_ptr<OrderParamaters>(
                                     new OrderParamaters(cparams1)
                             ),
                             nullptr, id_new );
        delete bndl.bracket_orders;
        bndl.bracket_orders = nullptr;
        break;

    case order_condition::one_triggers_other:
        _push_callback(callback_msg::trigger_OTO, bndl.exec_cb, id, id_new, 0, 0);
        assert( bndl.contingent_order );
        cparams1 = *bndl.contingent_order;
        _push_order_no_wait( cparams1.get_order_type(), cparams1.is_buy(),
                             cparams1.limit(), cparams1.stop(), cparams1.size(),
                             bndl.exec_cb, order_condition::none,
                             condition_trigger::none, nullptr, nullptr, id_new );
        delete bndl.contingent_order;
        bndl.contingent_order = nullptr;
        bndl.cond = order_condition::none;
        bndl.trigger = condition_trigger::none;
        break;

    case order_condition::trailing_stop:
        _push_callback(callback_msg::trigger_trailing_stop, bndl.exec_cb,
                       id, id_new, 0, 0);
        assert( bndl.contingent_order );
        cparams1 = *bndl.contingent_order;
        /* some OrderParamaters price trickery in here */
        _push_order_no_wait( order_type::stop, cparams1.is_buy(), 0,
                             _itop( _trailing_stop_from_params(cparams1) ),
                             cparams1.size(), bndl.exec_cb,
                             order_condition::_trailing_stop_active,
                             bndl.trigger,
                             std::unique_ptr<OrderParamaters>(
                                     new OrderParamaters(cparams1)
                                     ),
                             nullptr, id_new);
        delete bndl.contingent_order;
        bndl.contingent_order = nullptr;
        break;

    default:
        throw std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OCO_order(order_queue_elem& e)
{
    assert( _order::is_OCO(e) || _order::is_active_bracket(e) );
    assert(e.type != order_type::market);
    assert(e.type != order_type::null);

    OrderParamaters *op = e.cparams1.get();
    assert(op);
    assert(op->get_order_type() != order_type::market);
    assert(op->get_order_type() != order_type::null);

    bool partial_ok = (e.cond_trigger == condition_trigger::fill_partial);

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_order(e, partial_ok) ){
        /* if bracket issue trigger_BRACKET_close callback msg */
        callback_msg msg = _order::is_active_bracket(e)
                         ? callback_msg::trigger_BRACKET_close
                         : callback_msg::trigger_OCO;
        _push_callback(msg, e.exec_cb, e.id, e.id, 0, 0);
        return;
    }

    id_type id2 = _generate_id();
    /* if injection of primary order doesn't fill immediately, we need
     * to construct a new queue elem from cparams1, with a new ID. */
    order_queue_elem e2 = {
        op->get_order_type(),
        op->is_buy(),
        op->limit(),
        op->stop(),
        op->size(),
        e.exec_cb,
        e.cond,
        e.cond_trigger,
        nullptr,
        nullptr,
        id2,
        std::move(std::promise<id_type>())
    };

    /* if we fill second order immediately, remove first */
    if( _inject_order(e2, partial_ok) ){
        /* if we bracket issue trigger_BRACKET_close callback msg */
        callback_msg msg = _order::is_active_bracket(e)
                         ? callback_msg::trigger_BRACKET_close
                         : callback_msg::trigger_OCO;
        _push_callback(msg, e.exec_cb, e.id, id2, 0, 0);
        double price = _order::is_limit(e) ? e.limit : e.stop;
        _pull_order(e.id, price, false, _order::is_limit(e));
        return;
    }

    /* find the relevant orders that were previously injected */
    _order_bndl& o1 = _find(e.id, _order::is_limit(e));
    _order_bndl& o2 = _find(id2, _order::is_limit(e2));
    assert(o1);
    assert(o2);

    /* link each order with the other */
    o1.linked_order = new order_location(e2, false);
    o2.linked_order = new order_location(e, true);

    /* if bracket set _bracket_active condition (basically an OCO) */
    o1.cond = o2.cond = e.cond;
    o1.trigger = o2.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OTO_order(order_queue_elem& e)
{
    assert( (e.cond == order_condition::one_triggers_other)
            || (e.cond == order_condition::bracket)
            || (e.cond == order_condition::trailing_stop) );

    bool partial_ok = (e.cond_trigger == condition_trigger::fill_partial);
    /* if we fill immediately we need to insert other order from here */
    if( _inject_order(e, partial_ok) ){
        _insert_OTO_on_immediate_trigger(e);
        return;
    }

    _order_bndl& o = _find(e.id, (e.type == order_type::limit));
    assert(o);

    switch(e.cond){
    case order_condition::bracket:
        o.bracket_orders = new std::pair<OrderParamaters, OrderParamaters>(
                *e.cparams1.get(), *e.cparams2.get()
                );
        break;
    case order_condition::trailing_stop: /* no break */
    case order_condition::one_triggers_other:
        o.contingent_order = new OrderParamaters(*e.cparams1.get());
        break;
    default:
        throw std::runtime_error("invalid order condition");
    }

    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OTO_on_immediate_trigger(order_queue_elem& e)
{
    OrderParamaters *op = e.cparams1.get();
    OrderParamaters *op2;
    assert(op);

    id_type id2 = _generate_id();
    switch(e.cond){
    case order_condition::bracket:
        /*
         * if triggered order is a bracket we need to push order onto queue
         * FOR FAIRNESS
         *    1) use cparams2, which makes the 'target' order primary.
         *    2) The stop/loss (cparams1) order goes to cparams1.
         *    3) change the condition to _bracket_active (basically an OCO)
         *    4) keep trigger_condition the same
         *    5) use the new id
         */
        _push_callback(callback_msg::trigger_BRACKET_open, e.exec_cb, e.id, id2,
                       0, 0);
        op2 = e.cparams2.get();
        assert(op2);
        _push_order_no_wait( op2->get_order_type(), op2->is_buy(),
                             op2->limit(), op2->stop(), op2->size(),
                             e.exec_cb, order_condition::_bracket_active,
                             e.cond_trigger,
                             std::unique_ptr<OrderParamaters>(
                                     new OrderParamaters(*op)
                             ),
                             nullptr, id2);
        break;

    case order_condition::one_triggers_other:
        _push_callback(callback_msg::trigger_OTO, e.exec_cb, e.id, id2, 0, 0);
        _push_order_no_wait( op->get_order_type(), op->is_buy(),
                             op->limit(), op->stop(), op->size(),
                             e.exec_cb, order_condition::none,
                             condition_trigger::none, nullptr, nullptr, id2);
        break;

    case order_condition::trailing_stop:
        _push_callback(callback_msg::trigger_trailing_stop, e.exec_cb,
                       e.id, id2, 0, 0);
        /* some OrderParamaters price trickery in here */
        _push_order_no_wait( order_type::stop, op->is_buy(), 0,
                             _itop( _trailing_stop_from_params(*op) ),
                             op->size(), e.exec_cb,
                             order_condition::_trailing_stop_active,
                             e.cond_trigger,
                             std::unique_ptr<OrderParamaters>(
                                    new OrderParamaters(*op)
                             ),
                             nullptr, id2);
        break;

    default:
        throw std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_FOK_order(order_queue_elem& e)
{
    assert(e.type == order_type::limit);
    plevel p = _ptoi(e.limit);
    assert(p);
    size_t sz = (e.cond_trigger == condition_trigger::fill_partial) ? 0 : e.sz;
    /*
     * a bit of trickery here; if all we need is partial fill we check
     * if size of 0 is fillable; if p is <= _bid or >= _ask (and they are
     * valid) we know there's at least 1 order available to trade against
     */
    if( !_limit_exec<>::fillable(this, p, sz, e.is_buy) ){
        _push_callback(callback_msg::kill, e.exec_cb, e.id, e.id, e.limit, e.sz);
        return;
    }
    _route_basic_order(e);
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_trailing_stop_order(order_queue_elem& e)
{
    stop_bndl bndl = stop_bndl(e.is_buy, 0, e.id, e.sz, e.exec_cb, e.cond,
                               e.cond_trigger);
    OrderParamaters *op = e.cparams1.get();
    assert(op);
    bndl.nticks = nticks_from_params(*op);
    plevel p = _ptoi(e.stop);
    e.is_buy
        ? _chain<stop_chain_type>::template push<true>(this, p, std::move(bndl))
        : _chain<stop_chain_type>::template push<false>(this, p, std::move(bndl));
    _trailing_stop_insert(e.id, e.is_buy);
}


SOB_TEMPLATE
template<bool BuyLimit>
sob::fill_type
SOB_CLASS::_insert_limit_order( plevel limit,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                id_type id )
{
    fill_type fill = fill_type::none;
    size_t rmndr = size;
    if( (BuyLimit && limit >= _ask) || (!BuyLimit && limit <= _bid) ){
        /* If there are matching orders on the other side fill @ market
               - pass ref to callback functor, we'll copy later if necessary
               - return what we couldn't fill @ market */
        rmndr = _trade<!BuyLimit>(limit, id, size, exec_cb);
    }

    if( rmndr > 0) {
        /* insert what remains as limit order */
        _chain<limit_chain_type>::template
            push<BuyLimit>(this, limit, limit_bndl(id, rmndr, exec_cb) );
        if( rmndr < size ){
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
SOB_CLASS::_insert_market_order( size_t size,
                                 order_exec_cb_type exec_cb,
                                 id_type id )
{
    size_t rmndr = _trade<!BuyMarket>(nullptr, id, size, exec_cb);
    if( rmndr > 0 ){
        throw liquidity_exception( size, rmndr, id, "_insert_market_order()" );
    }
}


SOB_TEMPLATE
template<bool BuyStop>
void
SOB_CLASS::_insert_stop_order( plevel stop,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               id_type id )
{
   /*  we need an actual trade @/through the stop, i.e can't assume
       it's already been triggered by where last/bid/ask is...
       simply pass the order to the appropriate stop chain  */
    stop_bndl bndl = stop_bndl(BuyStop, limit, id, size, exec_cb);
    _chain<stop_chain_type>::template push<BuyStop>(this, stop, std::move(bndl));
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
         * trailing stop, which is transferred to the new order so it be can
         * constructed on execution, using that price
         */
        order_condition oc = order_condition::none;
        condition_trigger ct = condition_trigger::none;
        std::unique_ptr<OrderParamaters> op = nullptr;
        if( _order::is_trailing_stop(e) ){
            oc = e.cond;
            ct = e.trigger;
            op = std::unique_ptr<OrderParamaters>(
                    new OrderParamaters( *e.contingent_order )
                    );
        }

        /*
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; we simply increment _noutstanding_orders instead
        * and block on that when necessary.
        */
        _push_order_no_wait( ot, e.is_buy, limit, 0, sz, cb, oc, ct,
                             std::move(op), nullptr, id_new );

        /* we need to trigger new orders AFTER we push the market/limit */
        if( _order::is_advanced(e) && !_order::is_trailing_stop(e) ) {
            assert(e.trigger != condition_trigger::none);
            _handle_advanced_order_trigger(e, id);
        }
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_adjust_trailing_stops(bool buy_stops)
{
    auto& ids = buy_stops ? _trailing_buy_stops : _trailing_sell_stops;
    for( auto id : ids ){
        plevel p = _id_to_plevel<stop_chain_type>(id);
        assert(p);
        stop_bndl bndl = _chain<stop_chain_type>::pop(this, p, id);
        assert( bndl );
        assert( bndl.is_buy == buy_stops );
        assert( bndl.cond == order_condition::_trailing_stop_active );
        assert( bndl.nticks );
        p = _last + (buy_stops ? bndl.nticks : -bndl.nticks);
        _push_callback(callback_msg::adjust_trailing_stop, bndl.exec_cb,
                       id, id, _itop(p), bndl.sz);
        buy_stops
            ? _chain<stop_chain_type>::template
                  push<true>(this, p, std::move(bndl))
            : _chain<stop_chain_type>::template
                  push<false>(this, p, std::move(bndl));
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
SOB_CLASS::_pull_order(id_type id, bool pull_linked, bool limits_first)
{
    return limits_first 
        ? (_pull_order<limit_chain_type>(id, pull_linked)
                || _pull_order<stop_chain_type>(id, pull_linked))
        : (_pull_order<stop_chain_type>(id, pull_linked)
                || _pull_order<limit_chain_type>(id, pull_linked));
}


SOB_TEMPLATE
template<typename ChainTy>
bool 
SOB_CLASS::_pull_order(id_type id, bool pull_linked)
{ 
    /* caller needs to hold lock on _master_mtx or race w/ callback queue */
    plevel p = _id_to_plevel<ChainTy>(id);
    if( !p ){
        return false;
    }
    _assert_plevel(p);    
    return _pull_order<ChainTy>(id, p, pull_linked);
}


// TODO use _order::find
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
bool
SOB_CLASS::_pull_order(id_type id, double price, bool pull_linked, bool is_limit)
{
    return is_limit
        ? _pull_order<limit_chain_type>(id, _ptoi(price), pull_linked)
        : _pull_order<stop_chain_type>(id, _ptoi(price), pull_linked);
}


SOB_TEMPLATE
template<typename ChainTy>
void
SOB_CLASS::_pull_linked_order(typename ChainTy::value_type& bndl)
{
    order_location *loc = bndl.linked_order;
    if( loc && bndl.cond == order_condition::one_cancels_other ){
        /* false to pull_linked; this side in process of being pulled */
        _pull_order(loc->id, loc->price, false, loc->is_limit_chain);
    }
}


SOB_TEMPLATE
template<typename ChainTy>
typename SOB_CLASS::plevel
SOB_CLASS::_id_to_plevel(id_type id) const
{
    try{
        auto p = _id_cache.at(id);
        if( _chain<ChainTy>::is_limit == p.second ){
            return _ptoi( p.first );
        }
    }catch(std::out_of_range&){}
    return nullptr;
}


SOB_TEMPLATE
template<typename ChainTy>
typename ChainTy::value_type&
SOB_CLASS::_find(id_type id) const
{
    plevel p = _id_to_plevel<ChainTy>(id);
    if( p ){
        return _order::template find<ChainTy>(p, id);
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

    out << "*** (" << sot << ") " << _chain<ChainTy>::as_order_type()
        << "s ***" << std::endl;
    for( ; h >= l; --h){
        auto c = _chain<ChainTy>::get(h);
        if( !c->empty() ){
            out << _itop(h);
            for( const auto& e : *c ){
               _order::dump(out, e);
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


SOB_TEMPLATE
template<typename ChainTy>
AdvancedOrderTicket
SOB_CLASS::_bndl_to_aot(const typename _chain<ChainTy>::bndl_type& bndl) const
{
    AdvancedOrderTicket aot = AdvancedOrderTicket::null;
    aot.change_condition(bndl.cond);
    aot.change_trigger(bndl.trigger);
    if( bndl.cond == order_condition::one_cancels_other){
        /* reconstruct OrderParamaters from order_location */
        id_type id = bndl.linked_order->id;
        plevel p = _id_to_plevel<ChainTy>(id);
        auto other = _order::template find<ChainTy>(p, id);
        OrderParamaters op = _order::as_order_params(this, p, other);
        aot.change_order1(op);
    }else if( bndl.cond == order_condition::one_triggers_other){
        aot.change_order1( *bndl.contingent_order );
    }
    return aot;
}


SOB_TEMPLATE
std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_aot_order(const OrderParamaters& order) const
{
    // _master_mtx should needs to be held for this
    double limit = 0.0;
    double stop = 0.0;
    try{
        switch( order.get_order_type() ){
        case order_type::stop_limit:
            stop = _tick_price_or_throw(order.stop(), "invalid stop price");
            /* no break */
        case order_type::limit:
            limit = _tick_price_or_throw(order.limit(), "invalid limit price");
            break;
        case order_type::stop:
            stop = _tick_price_or_throw(order.stop(), "invalid stop price");
            break;
        case order_type::market:
            break;
        default:
            throw advanced_order_error("invalid order type");
        };
    }catch(std::invalid_argument& e){
        throw advanced_order_error(e);
    }
    return std::unique_ptr<OrderParamaters>(
            new OrderParamaters(order.is_buy(), order.size(), limit, stop)
    );
}


SOB_TEMPLATE
std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_aot_order( bool buy,
                             size_t size,
                             const AdvancedOrderTicketTrailingStop& aot ) const
{
    // _master_mtx should needs to be held for this

    /*
     * we are abusing the concept of OrderParamaters a bit; we need to pass
     * number of ticks for trailing stop to the book internals so we build
     * an OrderParamaters with an arbitrary max limit price and a stop adjusted
     * by 'nticks'; then in the orderbook we subtract the two.
     */

    long nticks = static_cast<long>(aot.nticks()); /* aot checks for overflow */
    if( nticks > ticks_in_range() ){
        throw advanced_order_error("trailing stop is too large for book");
    }
    return std::unique_ptr<OrderParamaters>(
            new OrderParamaters( _params_from_nticks(buy, size, nticks) )
    );
}


SOB_TEMPLATE
OrderParamaters
SOB_CLASS::_params_from_nticks( bool buy, size_t size, long nticks) const
{
    TickPrice<TickRatio> limit(max_price());
    TickPrice<TickRatio> stop = limit - nticks;
    return OrderParamaters(buy, size, limit, stop);
}


SOB_TEMPLATE
void
SOB_CLASS::_check_oco_limit_order( bool buy,
                                   double limit,
                                   std::unique_ptr<OrderParamaters> & op ) const
{
    order_type ot = op->get_order_type();
    if( ot == order_type::market ){
        throw advanced_order_error("OCO limit/market not valid order type");
    }else if( ot != order_type::limit ){
        return;
    }

    if( buy && !op->is_buy() && limit >= op->limit() ){
        throw advanced_order_error("OCO limit/limit buy price >= sell price");
    }else if( !buy && op->is_buy() && limit <= op->limit() ){
        throw advanced_order_error("OCO limit/limit sell price <= buy price");
    }else if( op->limit() == limit ){
         throw advanced_order_error("OCO limit/limit of same price" );
    }
}

SOB_TEMPLATE
size_t
SOB_CLASS::nticks_from_params(const OrderParamaters& params)
{
    long n = ( TickPrice<TickRatio>(params.limit()) -
               TickPrice<TickRatio>(params.stop()) ).as_ticks();
    assert(n > 0);
    return static_cast<size_t>(n);
}

};

#undef SOB_TEMPLATE
#undef SOB_CLASS
