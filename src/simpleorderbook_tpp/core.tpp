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
           e.is_buy indicates to check limits first (not buy/sell)
           success/fail is returned in the in e.id*/
        return _pull_order(e.id, true, e.is_buy);
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

// TODO figure out how to handle race condition concerning callbacks and
//      ID changes when an advanced order fills immediately(upon _inject)
//      and we callback with new ID BEFORE the user gets the original ID
//      from the initial call !
SOB_TEMPLATE
void
SOB_CLASS::_route_advanced_order(const order_queue_elem& e)
{
    switch(e.cond){
    case order_condition::_bracket_active: /* no break */
    case order_condition::one_cancels_other:
        _insert_OCO_order(e);
        break;
    case order_condition::trailing_stop:
        _insert_TRAILING_STOP_order(e);
        break;
    case order_condition::trailing_bracket:
        _insert_TRAILING_BRACKET_order(e);
        break;
    case order_condition::bracket:
        _insert_BRACKET_order(e);
        break;
    case order_condition::one_triggers_other:
        _insert_OTO_order(e);
        break;
    case order_condition::fill_or_kill:
        _insert_FOK_order(e);
        break;
    case order_condition::_trailing_stop_active:
        _insert_TRAILING_STOP_ACTIVE_order(e);
        break;
    case order_condition::_trailing_bracket_active:
        _insert_TRAILING_BRACKET_ACTIVE_order(e);
        break;
    default:
        throw std::runtime_error("invalid advanced order condition");
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
bool
SOB_CLASS::_handle_advanced_order_trigger(_order_bndl& bndl, id_type id)
{
    switch(bndl.cond){
    case order_condition::one_cancels_other:
    case order_condition::_bracket_active:
    case order_condition::_trailing_bracket_active:
    case order_condition::_trailing_stop_active:
        return false; /* NO OP */
    case order_condition::one_triggers_other:
        _handle_OTO(bndl, id);
        break;
    case order_condition::trailing_bracket:
        _handle_TRAILING_BRACKET(bndl, id);
        break;
    case order_condition::bracket:
        _handle_BRACKET(bndl, id);
        break;
    case order_condition::trailing_stop:
        _handle_TRAILING_STOP(bndl, id);
        break;
    default:
        throw std::runtime_error("invalid order condition");
    }
    return true;
}


SOB_TEMPLATE
bool
SOB_CLASS::_handle_advanced_order_cancel(_order_bndl& bndl, id_type id)
{
    switch(bndl.cond){
    case order_condition::one_triggers_other:
    case order_condition::bracket:
    case order_condition::trailing_bracket:
    case order_condition::trailing_stop:
    case order_condition::_trailing_stop_active:
        return false; /* NO OP */
    case order_condition::one_cancels_other:
    case order_condition::_trailing_bracket_active:
    case order_condition::_bracket_active:
        _handle_OCO(bndl, id);
        return true;
    default:
        throw std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OTO(_order_bndl& bndl, id_type id)
{
    const OrderParamaters* op1 = bndl.contingent_order;
    assert( op1 );

    _exec_OTO_order(op1, bndl.exec_cb, id);

    delete bndl.contingent_order;
    bndl.contingent_order = nullptr;
    bndl.cond = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_BRACKET(_order_bndl& bndl, id_type id)
{
    assert(bndl.price_bracket_orders);
    const OrderParamaters* op1 = &(bndl.price_bracket_orders->first);
    assert( op1->is_stop_order() );
    const OrderParamaters* op2 = &(bndl.price_bracket_orders->second);
    assert( op2->is_limit_order() );

    _exec_BRACKET_order(op1, op2, bndl.exec_cb, bndl.trigger, id);

    /*
     * delete what bracket_orders points at; the order is now of condition
     * _bracket_active, which needs to be treated as an OCO, and use
     * linked_order pointer in the anonymous union.
     */
    delete bndl.price_bracket_orders;
    bndl.price_bracket_orders = nullptr;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_TRAILING_BRACKET(_order_bndl& bndl, id_type id)
{
    assert(bndl.nticks_bracket_orders);
    const OrderParamaters* op1 = &(bndl.nticks_bracket_orders->first);
    assert( op1->is_stop_order() );
    const OrderParamaters* op2 = &(bndl.nticks_bracket_orders->second);
    assert( op2->is_limit_order() );

    _exec_TRAILING_BRACKET_order(op1, op2, bndl.exec_cb, bndl.trigger, id);

    /*
     * delete what bracket_orders points at; the order is now of condition
     * _trailing_bracket_active, which needs to be treated as an OCO, and use
     * linked_order pointer in the anonymous union.
     */
    delete bndl.nticks_bracket_orders;
    bndl.nticks_bracket_orders = nullptr;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_TRAILING_STOP(_order_bndl& bndl, id_type id)
{
    const OrderParamaters* op1 = bndl.contingent_order;
    assert( op1 );

    _exec_TRAILING_STOP_order(op1, bndl.exec_cb, bndl.trigger, id);

    delete bndl.contingent_order;
    bndl.contingent_order = nullptr;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OCO(_order_bndl& bndl, id_type id)
{
    assert( _order::is_OCO(bndl)
            || _order::is_active_bracket(bndl)
            || _order::is_active_trailing_bracket(bndl) );

    order_location *loc = _order::is_active_trailing_bracket(bndl)
                        ? &(bndl.linked_trailer->second)
                        : bndl.linked_order;
    assert(loc);

    id_type id_old = loc->is_primary ? loc->id : id;

    _exec_OCO_order(bndl, id_old, id, loc->id, loc->price, loc->is_limit_chain);

    /* remove linked order from union */
    if( _order::is_active_trailing_bracket(bndl) ){
        delete bndl.linked_trailer;
        bndl.linked_trailer = nullptr;
    }else{
        delete bndl.linked_order;
        bndl.linked_order = nullptr;
    }
    bndl.cond = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


SOB_TEMPLATE
void
SOB_CLASS::_exec_OTO_order(const OrderParamaters *op,
                           const order_exec_cb_type& cb,
                           id_type id)
{
    assert( op->is_by_price() );

    id_type id_new  = _generate_id();
    _push_callback(callback_msg::trigger_OTO, cb, id, id_new , 0, 0);

    _push_order_no_wait( op->get_order_type(), op->is_buy(),
                         op->limit_price(), op->stop_price(), op->size(), cb,
                         order_condition::none, condition_trigger::none,
                         nullptr, nullptr, id_new );
}


SOB_TEMPLATE
void
SOB_CLASS::_exec_BRACKET_order(const OrderParamaters *op1,
                               const OrderParamaters *op2,
                               const order_exec_cb_type& cb,
                               condition_trigger trigger,
                               id_type id)
{
    /*
     * push order onto queue FOR FAIRNESS
     *    1) use second order that bndl.bracket_orders points at,
     *       which makes the 'target' order primary
     *    2) the first order (stop/loss) is then used for cparams1
     *    3) change the condition to _bracket_active (basically an OCO)
     *    4) keep trigger_condition the same
     *    5) use the new id
     */
    assert( op1->is_by_price() );
    assert( op2->is_by_price() );

    id_type id_new = _generate_id();
    _push_callback(callback_msg::trigger_BRACKET_open, cb, id, id_new , 0, 0);

    auto new_op = std::unique_ptr<OrderParamaters>( op1->copy_new() );

    _push_order_no_wait( op2->get_order_type(), op2->is_buy(),
                         op2->limit_price(), op2->stop_price(), op2->size(),
                         cb, order_condition::_bracket_active,
                         trigger, std::move(new_op), nullptr, id_new );
}


SOB_TEMPLATE
void
SOB_CLASS::_exec_TRAILING_BRACKET_order(const OrderParamaters *op1,
                                        const OrderParamaters *op2,
                                        const order_exec_cb_type& cb,
                                        condition_trigger trigger,
                                        id_type id)
{
    /* (see comments from _exec_BRACKET_order) */
    assert( op1->is_by_nticks() );
    assert( op2->is_by_nticks() );

    id_type id_new = _generate_id();
    _push_callback(callback_msg::trigger_BRACKET_open, cb, id, id_new, 0, 0);

    plevel l = _generate_trailing_limit(op2->is_buy(), op2->limit_nticks());
    assert(l);

    auto new_op = std::unique_ptr<OrderParamaters>( op1->copy_new() );

    _push_order_no_wait( order_type::limit, op2->is_buy(), _itop(l),
                         0, op2->size(), cb,
                         order_condition::_trailing_bracket_active,
                         trigger, std::move(new_op), nullptr, id_new );
}


SOB_TEMPLATE
void
SOB_CLASS::_exec_TRAILING_STOP_order(const OrderParamaters *op,
                                     const order_exec_cb_type& cb,
                                     condition_trigger trigger,
                                     id_type id)
{
    assert( op->is_by_nticks() );

    id_type id_new = _generate_id();
    _push_callback(callback_msg::trigger_trailing_stop, cb, id, id_new, 0, 0);

    plevel stop = _generate_trailing_stop(op->is_buy(), op->stop_nticks());
    assert(stop);

    auto new_op = std::unique_ptr<OrderParamaters>( op->copy_new() );

    _push_order_no_wait( order_type::stop, op->is_buy(), 0, _itop(stop),
                         op->size(), cb,
                         order_condition::_trailing_stop_active,
                         trigger, std::move(new_op), nullptr, id_new);
}


SOB_TEMPLATE
template<typename T>
void
SOB_CLASS::_exec_OCO_order(const T& t,
                           id_type id_old,
                           id_type id_new,
                           id_type id_pull,
                           double price_pull,
                           bool is_limit)
{
    callback_msg msg = _order::is_OCO(t)
                     ? callback_msg::trigger_OCO
                     : callback_msg::trigger_BRACKET_close;
    _push_callback(msg, t.exec_cb, id_old, id_new, 0, 0);

    if( id_pull ){
        assert( price_pull );
        /* remove primary order, BE SURE pull_linked=false */
        _pull_order(id_pull, price_pull, false, is_limit);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OCO_order(const order_queue_elem& e)
{
    bool is_limit = _order::is_limit(e);
    assert( _order::is_OCO(e) || _order::is_active_bracket(e) );
    assert( is_limit || _order::is_stop(e) );

    const OrderParamaters *op = e.cparams1.get();
    assert(op);
    assert(op->get_order_type() != order_type::market);
    assert(op->get_order_type() != order_type::null);

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_OCO_order(e, e.id, e.id, 0, 0, is_limit);
        return;
    }

    /* construct a new queue elem from cparams1, with a new ID. */
    id_type id2 = _generate_id();
    order_queue_elem e2 = {
        op->get_order_type(), op->is_buy(), op->limit_price(),
        op->stop_price(), op->size(), e.exec_cb, e.cond, e.cond_trigger,
        nullptr, nullptr, id2, std::move(std::promise<id_type>())
    };

    /* if we fill second order immediately, remove first */
    if( _inject_order(e2, _order::needs_partial_fill(e)) ){
        _exec_OCO_order(e, e.id, id2, e.id, _order::index_price(e), is_limit);
        return;
    }

    /* find the relevant orders that were previously injected */
    _order_bndl& o1 = _find(e.id, _order::is_limit(e));
    assert(o1);
    _order_bndl& o2 = _find(id2, _order::is_limit(e2));
    assert(o2);

    /* link each order with the other */
    o1.linked_order = new order_location(e2, false);
    o2.linked_order = new order_location(e, true);

    /* transfer condition/trigger info */
    o1.cond = o2.cond = e.cond;
    o1.trigger = o2.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OTO_order(const order_queue_elem& e)
{
    assert( _order::is_OTO(e) );
    const OrderParamaters *op = e.cparams1.get();
    assert( op );
    assert( op->is_by_price() );

    /* if we fill immediately we need to insert other order from here */
    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_OTO_order(op, e.exec_cb, e.id);
        return;
    }

    _order_bndl& o = _find(e.id, _order::is_limit(e));
    assert(o);
    o.contingent_order = op->copy_new();
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_BRACKET_order(const order_queue_elem& e)
{
    assert( _order::is_bracket(e) );
    const OrderParamaters *op1 = e.cparams1.get();
    assert( op1 );
    assert( op1->is_by_price() );
    const OrderParamaters *op2 = e.cparams2.get();
    assert( op2 );
    assert( op2->is_by_price() );

    /* if we fill immediately we need to insert other order from here */
    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_BRACKET_order(op1, op2, e.exec_cb, e.cond_trigger, e.id);
        return;
    }

    _order_bndl& o = _find(e.id, _order::is_limit(e));
    assert(o);
    o.price_bracket_orders = new price_bracket_type(
            reinterpret_cast<const OrderParamatersByPrice&>(*op1),
            reinterpret_cast<const OrderParamatersByPrice&>(*op2)
            );
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_TRAILING_BRACKET_order(const order_queue_elem& e)
{
    assert( _order::is_trailing_bracket(e) );
    const OrderParamaters *op1 = e.cparams1.get();
    assert(op1);
    const OrderParamaters *op2 = e.cparams2.get();
    assert(op2);

    /* if we fill immediately we need to insert other order from here */
    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_TRAILING_BRACKET_order(op1, op2, e.exec_cb, e.cond_trigger, e.id);
        return;
    }

    _order_bndl& o = _find(e.id, _order::is_limit(e));
    assert(o);
    o.nticks_bracket_orders = new nticks_bracket_type(
            reinterpret_cast<const OrderParamatersByNTicks&>(*op1),
            reinterpret_cast<const OrderParamatersByNTicks&>(*op2)
            );
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_TRAILING_STOP_order(const order_queue_elem& e)
{
    assert( _order::is_trailing_stop(e) );
    const OrderParamaters *op = e.cparams1.get();
    assert(op);

    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_TRAILING_STOP_order(op, e.exec_cb, e.cond_trigger, e.id);
        return;
    }

    _order_bndl& o = _find(e.id, _order::is_limit(e));
    assert(o);
    o.contingent_order = op->copy_new();
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_TRAILING_BRACKET_ACTIVE_order(const order_queue_elem& e)
{
     assert(  _order::is_active_trailing_bracket(e) );
     assert( _order::is_limit(e) || _order::is_stop(e) );

     const OrderParamaters *op = e.cparams1.get();
     assert(op);
     assert(op->get_order_type() != order_type::market);
     assert(op->get_order_type() != order_type::null);

     /* if we fill immediately, no need to enter 2nd order */
     if( _inject_order(e, _order::needs_partial_fill(e)) ){
         _push_callback(callback_msg::trigger_BRACKET_close, e.exec_cb,
                 e.id, e.id, 0, 0);
         return;
     }

     /* find the relevant order previously injected */
     _order_bndl& o1 = _find(e.id, _order::is_limit(e));
     assert(o1);

     id_type id2 = _generate_id();
     size_t nticks = op->stop_nticks();
     plevel p = _generate_trailing_stop(op->is_buy(), nticks);

     stop_bndl o2 = stop_bndl(op->is_buy(), 0, id2, op->size(), e.exec_cb,
                                order_condition::_trailing_bracket_active,
                                e.cond_trigger);

     /* link each order with the other */
     o1.linked_trailer = new linked_trailer_type(
             0,
             order_location(false, _itop(p), id2, false)
             );

     o2.linked_trailer = new linked_trailer_type(
             nticks,
             order_location(e, true)
             );

     /* transfer condition/trigger info */
     o1.cond = o2.cond = e.cond;
     o1.trigger = o2.trigger = e.cond_trigger;

     _chain<stop_chain_type>::push(this, p, std::move(o2));
     _trailing_stop_insert(id2, op->is_buy());
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_TRAILING_STOP_ACTIVE_order(const order_queue_elem& e)
{
    assert( _order::is_active_trailing_stop(e) );

    stop_bndl bndl = stop_bndl(e.is_buy, 0, e.id, e.sz, e.exec_cb, e.cond,
                               e.cond_trigger);

    const OrderParamaters *op = e.cparams1.get();
    assert(op);

    bndl.nticks = op->stop_nticks();
    plevel p = _generate_trailing_stop(op->is_buy(), bndl.nticks);
    _chain<stop_chain_type>::push(this, p, std::move(bndl));
    _trailing_stop_insert(e.id, e.is_buy);
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_FOK_order(const order_queue_elem& e)
{
    assert( _order::is_limit(e) );
    plevel p = _ptoi(e.limit);
    assert(p);
    size_t sz = _order::needs_partial_fill(e) ? 0 : e.sz;
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
void
SOB_CLASS::_adjust_trailing_stop(id_type id, bool buy_stop)
{
    plevel p = _id_to_plevel<stop_chain_type>(id);
    assert(p);

    stop_bndl bndl = _chain<stop_chain_type>::pop(this, p, id);
    assert( bndl );
    assert( bndl.nticks );
    assert( bndl.is_buy == buy_stop );
    assert( _order::is_active_trailing_stop(bndl)
            || _order::is_active_trailing_bracket(bndl) );

    size_t nticks = _order::is_active_trailing_stop(bndl)
                  ? bndl.nticks
                  : bndl.linked_trailer->first;
    p = _generate_trailing_stop(buy_stop, nticks);
    double price = _itop(p);

    _push_callback(callback_msg::adjust_trailing_stop, bndl.exec_cb,
                   id, id, price, bndl.sz);

    /* if bracket, need to let linked order know new location */
    if( _order::is_active_trailing_bracket(bndl) ){
        assert( bndl.linked_trailer );
        const order_location& loc = bndl.linked_trailer->second;
        auto& linked = _find(loc.id, loc.is_limit_chain);
        assert( _order::is_active_trailing_bracket(linked) );
        linked.linked_trailer->second.price = price;
    }

    _chain<stop_chain_type>::push(this, p, std::move(bndl));
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
    if( loc && _order::is_OCO(bndl) ){
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


SOB_TEMPLATE
template<typename ChainTy>
AdvancedOrderTicket
SOB_CLASS::_bndl_to_aot(const typename _chain<ChainTy>::bndl_type& bndl) const
{
    AdvancedOrderTicket aot = AdvancedOrderTicket::null;
    aot.change_condition(bndl.cond);
    aot.change_trigger(bndl.trigger);

    order_location *loc = nullptr;

    switch( bndl.cond ){
    case order_condition::_bracket_active: /* no break */
    case order_condition::one_cancels_other:
        loc = bndl.linked_order;
        break;

    case order_condition::_trailing_bracket_active:
        loc = &(bndl.linked_trailer->second);
        break;

    case order_condition::trailing_stop: /* no break */
    case order_condition::one_triggers_other:
        aot.change_order1( *bndl.contingent_order );
        break;

    case order_condition::trailing_bracket:
        aot.change_order1( bndl.nticks_bracket_orders->first );
        aot.change_order2( bndl.nticks_bracket_orders->second );
        break;

    case order_condition::bracket:
        aot.change_order1( bndl.price_bracket_orders->first );
        aot.change_order2( bndl.price_bracket_orders->second );
        break;

    case order_condition::fill_or_kill: /* no break */
    case order_condition::_trailing_stop_active: /* no break */
    case order_condition::none:
        break;
    };

    if( loc ){
        /* reconstruct OrderParamaters from order_location */
        aot.change_order1(
            loc->is_limit_chain
               ? _order::template as_price_params<limit_chain_type>(this, loc->id)
               : _order::template as_price_params<stop_chain_type>(this, loc->id)
        );
    }

    return aot;
}


SOB_TEMPLATE
std::pair<std::unique_ptr<OrderParamaters>,
          std::unique_ptr<OrderParamaters>>
SOB_CLASS::_build_advanced_params(bool buy,
                                  size_t size,
                                  const AdvancedOrderTicket& advanced) const
{
    std::unique_ptr<OrderParamaters> pp1;
    std::unique_ptr<OrderParamaters> pp2;

    switch( advanced.condition() ){
    case order_condition::trailing_bracket:
        pp2 = _build_nticks_params(!buy, size, advanced.order2());
        /* no break */
    case order_condition::trailing_stop:
        pp1 = _build_nticks_params(!buy, size, advanced.order1());
        break;

    case order_condition::bracket:
        pp2 = _build_price_params(advanced.order2());
        /* no break */
    case order_condition::one_triggers_other: /* no break */
    case order_condition::one_cancels_other:
        pp1 = _build_price_params(advanced.order1());
        break;

    case order_condition::fill_or_kill:
        break;
    default:
        throw advanced_order_error("invalid order condition");
    };

    return std::make_pair(std::move(pp1), std::move(pp2));
}

SOB_TEMPLATE
std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_nticks_params(bool buy,
                               size_t size,
                               const OrderParamaters *order) const
{
    assert( order->is_by_nticks() );

    if( static_cast<long>(order->limit_nticks()) > ticks_in_range() ){
        throw advanced_order_error("limit_nticks too large");
    }
    if( static_cast<long>(order->stop_nticks()) > ticks_in_range() ){
        throw advanced_order_error("stop_nticks too large");
    }

    return std::unique_ptr<OrderParamaters>(
            new OrderParamatersByNTicks(buy, size, order->limit_nticks(),
                    order->stop_nticks())
    );
}


SOB_TEMPLATE
std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_price_params(const OrderParamaters *order) const
{
    assert( order->is_by_price() );

    if( !order->size() ){
        throw advanced_order_error("invalid order size");
    }

    double limit = 0.0;
    double stop = 0.0;
    try{
        switch( order->get_order_type() ){
        case order_type::stop_limit:
            stop = _tick_price_or_throw(order->stop_price(), "invalid stop price");
            /* no break */
        case order_type::limit:
            limit = _tick_price_or_throw(order->limit_price(), "invalid limit price");
            break;
        case order_type::stop:
            stop = _tick_price_or_throw(order->stop_price(), "invalid stop price");
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
            new OrderParamatersByPrice(order->is_buy(), order->size(), limit, stop)
    );
}


SOB_TEMPLATE
void
SOB_CLASS::_check_limit_order( bool buy,
                               double limit,
                               std::unique_ptr<OrderParamaters> & op,
                               order_condition oc) const
{
    assert( op->is_by_price() );

    if( !op->size() ){
        throw advanced_order_error(oc + " invalid order size");
    }

    order_type ot = op->get_order_type();
    if( ot == order_type::market ){
        throw advanced_order_error(oc + " limit/market not valid order type");
    }else if( ot != order_type::limit ){
        return;
    }

    if( buy && !op->is_buy() && limit >= op->limit_price() ){
        throw advanced_order_error(oc + " limit/limit buy price >= sell price");
    }else if( !buy && op->is_buy() && limit <= op->limit_price() ){
        throw advanced_order_error(oc + " limit/limit sell price <= buy price");
    }else if( op->limit_price() == limit ){
         throw advanced_order_error(oc + " limit/limit of same price" );
    }
}


};

#undef SOB_TEMPLATE
#undef SOB_CLASS
