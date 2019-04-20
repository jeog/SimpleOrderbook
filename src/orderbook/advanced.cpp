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

#include "../../include/simpleorderbook.hpp"

#define SOB_CLASS SimpleOrderbook::SimpleOrderbookBase

// NOTE - only explicitly instantiate members needed for link and not
//        done implicitly. If (later) called from outside advanced.cpp
//        need to add them.

// TODO figure out how to handle race condition concerning callbacks and
//      ID changes when an advanced order fills immediately(upon _inject)
//      and we callback with new ID BEFORE the user gets the original ID
//      from the initial call !

// TODO allow for '_active' conditions to return initial advanced information
//      as order_info; i.e. trailing_stop should give you the 'nticks' value

namespace sob{

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
     * linked_trailer pointer in the anonymous union.
     */
    delete bndl.nticks_bracket_orders;
    bndl.nticks_bracket_orders = nullptr;
}


void
SOB_CLASS::_handle_TRAILING_STOP(_order_bndl& bndl, id_type id)
{
    const OrderParamaters* op1 = bndl.contingent_order;
    assert( op1 );

    _exec_TRAILING_STOP_order(op1, bndl.exec_cb, bndl.trigger, id);

    delete bndl.contingent_order;
    bndl.contingent_order = nullptr;
}


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
        _pull_order(id_pull, false);
    }
}


void
SOB_CLASS::_insert_OCO_order(const order_queue_elem& e)
{
    assert( _order::is_OCO(e) || _order::is_active_bracket(e) );
    assert( _order::is_limit(e) || _order::is_stop(e) );

    const OrderParamaters *op = e.cparams1.get();
    assert(op);
    assert(op->get_order_type() != order_type::market);
    assert(op->get_order_type() != order_type::null);

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_order(e, _order::needs_partial_fill(e)) ){
        _exec_OCO_order(e, e.id, e.id, 0, 0, _order::is_limit(e));
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
        _exec_OCO_order(e, e.id, id2, e.id, _order::index_price(e),
                        _order::is_limit(e));
        return;
    }

    /* find the relevant orders that were previously injected */
    _order_bndl& o1 = _find(e.id);
    assert(o1);
    _order_bndl& o2 = _find(id2);
    assert(o2);

    /* link each order with the other */
    o1.linked_order = new order_location(e2, false);
    o2.linked_order = new order_location(e, true);

    /* transfer condition/trigger info */
    o1.cond = o2.cond = e.cond;
    o1.trigger = o2.trigger = e.cond_trigger;
}


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

    _order_bndl& o = _find(e.id);
    assert(o);
    o.contingent_order = op->copy_new();
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


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

    _order_bndl& o = _find(e.id);
    assert(o);
    o.price_bracket_orders = new price_bracket_type(
            reinterpret_cast<const OrderParamatersByPrice&>(*op1),
            reinterpret_cast<const OrderParamatersByPrice&>(*op2)
            );
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


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

    _order_bndl& o = _find(e.id);
    assert(o);
    o.nticks_bracket_orders = new nticks_bracket_type(
            reinterpret_cast<const OrderParamatersByNTicks&>(*op1),
            reinterpret_cast<const OrderParamatersByNTicks&>(*op2)
            );
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


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

    _order_bndl& o = _find(e.id);
    assert(o);
    o.contingent_order = op->copy_new();
    o.cond = e.cond;
    o.trigger = e.cond_trigger;
}


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
     _order_bndl& o1 = _find(e.id);
     assert(o1);

     id_type id2 = _generate_id();
     size_t nticks = op->stop_nticks();
     plevel p = _generate_trailing_stop(op->is_buy(), nticks);

     stop_bndl o2(op->is_buy(), 0, id2, op->size(), e.exec_cb,
                  order_condition::_trailing_bracket_active,  e.cond_trigger);

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

     _chain_op<stop_chain_type>::push(this, p, std::move(o2));
     _trailing_stop_insert(id2, op->is_buy());
}


void
SOB_CLASS::_insert_TRAILING_STOP_ACTIVE_order(const order_queue_elem& e)
{
    assert( _order::is_active_trailing_stop(e) );

    stop_bndl bndl(e.is_buy, 0, e.id, e.sz, e.exec_cb, e.cond, e.cond_trigger);

    const OrderParamaters *op = e.cparams1.get();
    assert(op);

    bndl.nticks = op->stop_nticks();
    plevel p = _generate_trailing_stop(op->is_buy(), bndl.nticks);
    _chain_op<stop_chain_type>::push(this, p, std::move(bndl));
    _trailing_stop_insert(e.id, e.is_buy);
}


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
    if( !_limit_is_fillable(p, sz, e.is_buy) ){
        _push_callback(callback_msg::kill, e.exec_cb, e.id, e.id, e.limit, e.sz);
        return;
    }
    _route_basic_order(e);
}


void
SOB_CLASS::_adjust_trailing_stops(bool buy_stops)
{
    auto& ids = buy_stops ? _trailing_buy_stops : _trailing_sell_stops;
    for( auto id : ids ){
        _adjust_trailing_stop(id, buy_stops);
    }
}


void
SOB_CLASS::_adjust_trailing_stop(id_type id, bool buy_stop)
{
    stop_bndl bndl = _chain_op<stop_chain_type>::pop(this, id);
    assert( bndl );
    assert( bndl.nticks );
    assert( bndl.is_buy == buy_stop );
    assert( _order::is_active_trailing_stop(bndl)
            || _order::is_active_trailing_bracket(bndl) );

    size_t nticks = _order::is_active_trailing_stop(bndl)
                  ? bndl.nticks
                  : bndl.linked_trailer->first;
    plevel p = _generate_trailing_stop(buy_stop, nticks);
    double price = _itop(p);

    _push_callback(callback_msg::adjust_trailing_stop, bndl.exec_cb,
                   id, id, price, bndl.sz);

    /* if bracket, need to let linked order know new location */
    if( _order::is_active_trailing_bracket(bndl) ){
        assert( bndl.linked_trailer );
        const order_location& loc = bndl.linked_trailer->second;
        auto& linked = _find(loc.id);
        assert( _order::is_active_trailing_bracket(linked) );
        linked.linked_trailer->second.price = price;
    }

    _chain_op<stop_chain_type>::push(this, p, std::move(bndl));
}


AdvancedOrderTicket
SOB_CLASS::_bndl_to_aot(const _order_bndl& bndl) const
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

}; /* sob */

#undef SOB_CLASS
