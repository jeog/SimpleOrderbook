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
#include "../../include/order_util.hpp"
#include "specials.tpp"


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
SOB_CLASS::_route_advanced_order(order_queue_elem& e)
{
    switch(e.condition){
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
    case order_condition::all_or_nothing:
        _insert_ALL_OR_NOTHING_order(e);
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
    case order_condition::all_or_nothing:
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
    case order_condition::all_or_nothing:
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
    assert( bndl.contingent_order );

    _exec_OTO_order( *bndl.contingent_order, bndl.cb, id);

    delete bndl.contingent_order;
    bndl.contingent_order = nullptr;
    bndl.cond = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


void
SOB_CLASS::_handle_BRACKET(_order_bndl& bndl, id_type id)
{
    assert( bndl.price_bracket_orders );
    assert( bndl.price_bracket_orders->first.is_stop_order() );
    assert( bndl.price_bracket_orders->second.is_limit_order() );

    _exec_BRACKET_order( bndl.price_bracket_orders->first,
                         bndl.price_bracket_orders->second,
                         bndl.cb, bndl.trigger, id );
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
    assert( bndl.nticks_bracket_orders );
    assert( bndl.nticks_bracket_orders->first.is_stop_order() );
    assert( bndl.nticks_bracket_orders->second.is_limit_order() );

    _exec_TRAILING_BRACKET_order( bndl.nticks_bracket_orders->first,
                                  bndl.nticks_bracket_orders->second,
                                  bndl.cb, bndl.trigger, id );

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
    assert( bndl.contingent_order);

    _exec_TRAILING_STOP_order(*bndl.contingent_order, bndl.cb, bndl.trigger, id);

    delete bndl.contingent_order;
    bndl.contingent_order = nullptr;
}


void
SOB_CLASS::_handle_OCO(_order_bndl& bndl, id_type id)
{
    using namespace detail;

    bool is_atb = order::is_active_trailing_bracket(bndl);
    assert( order::is_OCO(bndl) || order::is_active_bracket(bndl) || is_atb );

    const order_location *loc;
    if( is_atb ){
        assert( bndl.linked_trailer );
        loc = &(bndl.linked_trailer->second);
    }else{
        assert( bndl.linked_order );
        loc = bndl.linked_order;
    }

    id_type id_old = loc->is_primary ? loc->id : id;

    _exec_OCO_order(bndl, id_old, id, loc->id, loc->price);

    /* remove linked order from union */
    if( is_atb ){
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
SOB_CLASS::_exec_OTO_order( const OrderParamaters& op,
                            const order_exec_cb_bndl& cb,
                            id_type id )
{
    assert( op.is_by_price() );

    id_type id_new  = _generate_id();
    _push_exec_callback(callback_msg::trigger_OTO, cb, id, id_new , 0, 0);

    _push_internal_order( op.get_order_type(), op.is_buy(),
                          op.limit_price(), op.stop_price(), op.size(), cb,
                          order_condition::none, condition_trigger::none,
                          nullptr, nullptr, id_new );
}


void
SOB_CLASS::_exec_BRACKET_order( const OrderParamaters& op1,
                                const OrderParamaters& op2,
                                const order_exec_cb_bndl& cb,
                                condition_trigger trigger,
                                id_type id )
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
    assert( op1.is_by_price() );
    assert( op2.is_by_price() );

    id_type id_new = _generate_id();
    _push_exec_callback( callback_msg::trigger_BRACKET_open, cb, id, id_new ,
                         0, 0 );

    _push_internal_order( op2.get_order_type(), op2.is_buy(),
                          op2.limit_price(), op2.stop_price(), op2.size(),
                          cb, order_condition::_bracket_active,
                          trigger, op1.copy_new(), nullptr, id_new );
}


void
SOB_CLASS::_exec_TRAILING_BRACKET_order( const OrderParamaters& op1,
                                         const OrderParamaters& op2,
                                         const order_exec_cb_bndl& cb,
                                         condition_trigger trigger,
                                         id_type id )
{
    /* (see comments from _exec_BRACKET_order) */
    assert( op1.is_by_nticks() );
    assert( op2.is_by_nticks() );

    id_type id_new = _generate_id();
    _push_exec_callback( callback_msg::trigger_BRACKET_open, cb, id, id_new,
                         0, 0 );

    plevel l = _trailing_limit_plevel( op2.is_buy(), op2.limit_nticks() );
    assert(l);

    _push_internal_order( order_type::limit, op2.is_buy(), _itop(l), 0,
                          op2.size(), cb,
                          order_condition::_trailing_bracket_active, trigger,
                          op1.copy_new() , nullptr, id_new );
}


void
SOB_CLASS::_exec_TRAILING_STOP_order( const OrderParamaters& op,
                                      const order_exec_cb_bndl& cb,
                                      condition_trigger trigger,
                                      id_type id )
{
    assert( op.is_by_nticks() );

    id_type id_new = _generate_id();
    _push_exec_callback( callback_msg::trigger_trailing_stop, cb, id, id_new,
                         0, 0 );

    plevel stop = _trailing_stop_plevel( op.is_buy(), op.stop_nticks() );
    assert(stop);

    _push_internal_order( order_type::stop, op.is_buy(), 0, _itop(stop),
                          op.size(), cb, order_condition::_trailing_stop_active,
                          trigger, op.copy_new(), nullptr, id_new );
}


template<typename T>
void
SOB_CLASS::_exec_OCO_order( const T& t,
                            id_type id_old,
                            id_type id_new,
                            id_type id_pull,
                            double price_pull )
{
    callback_msg msg = detail::order::is_OCO(t)
                     ? callback_msg::trigger_OCO
                     : callback_msg::trigger_BRACKET_close;
    _push_exec_callback(msg, t.cb, id_old, id_new, 0, 0);

    if( id_pull ){
        assert( price_pull );
        /* remove primary order, BE SURE pull_linked=false */
        _pull_order(id_pull, false);
    }
}


void
SOB_CLASS::_insert_OCO_order(order_queue_elem& e)
{
    using namespace detail;

    assert( order::is_OCO(e) || order::is_active_bracket(e) );
    assert( order::is_limit(e) || order::is_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->get_order_type() != order_type::market );
    assert( e.cparams1->get_order_type() != order_type::null );

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_basic_order(e, order::needs_partial_fill(e)) )
    {
        _exec_OCO_order(e, e.id, e.id, 0, 0);
        return;
    }

    /* construct a new queue elem from cparams1, with a new ID. */
    id_type id2 = _generate_id();
    order_queue_elem e2( e.cparams1->get_order_type(), e.cparams1->is_buy(),
                         e.cparams1->limit_price(), e.cparams1->stop_price(),
                         e.cparams1->size(), e.cb, id2, e.condition,
                         e.trigger, nullptr, nullptr );

    /* if we fill second order immediately, remove first */
    if( _inject_basic_order(e2, order::needs_partial_fill(e)) )
    {
        _exec_OCO_order( e, e.id, id2, e.id, order::index_price(e) );
        /*
         *  May 7 2019 - return order ID of other order
         *
         *  this fixes the problem of the order-id change callback occurring
         *  before the initial insert order method returns with the original ID
         */
        e.id = id2;
        return;
    }

    /* find the relevant orders that were previously injected */
    auto& order1 = _id_cache.at(e.id);
    assert(order1);

    auto& order2 = _id_cache.at(id2);
    assert(order2);

    /* link each order with the other */
    order1->linked_order = new order_location(e2, false);
    order2->linked_order = new order_location(e, true);

    /* transfer condition/trigger info */
    order1->cond = order2->cond = e.condition;
    order1->trigger = order2->trigger = e.trigger;
}


void
SOB_CLASS::_insert_OTO_order(const order_queue_elem& e)
{
    assert( detail::order::is_OTO(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_price() );

    /* if we fill immediately we need to insert other order from here */
    if( _inject_basic_order(e, detail::order::needs_partial_fill(e)) )
    {
        _exec_OTO_order(*e.cparams1, e.cb, e.id);
        /*
         * TODO id replace like OCO ?
         */
        return;
    }

    auto& order = _id_cache.at(e.id);
    assert(order);
    order->contingent_order = e.cparams1->copy_new().release(); // TODO
    order->cond = e.condition;
    order->trigger = e.trigger;
}


void
SOB_CLASS::_insert_BRACKET_order(const order_queue_elem& e)
{
    assert( detail::order::is_bracket(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_price() );
    assert( e.cparams2 );
    assert( e.cparams2->is_by_price() );

    /* if we fill immediately we need to insert other order from here */
    if( _inject_basic_order(e, detail::order::needs_partial_fill(e)) )
    {
        _exec_BRACKET_order(*e.cparams1, *e.cparams2, e.cb, e.trigger, e.id);
        /*
         * TODO ID replace like OCO ?
         */
        return;
    }

    auto& order = _id_cache.at(e.id);
    assert(order);

    order->price_bracket_orders = new price_bracket_type(
            reinterpret_cast<const OrderParamatersByPrice&>(*e.cparams1),
            reinterpret_cast<const OrderParamatersByPrice&>(*e.cparams2)
            );
    order->cond = e.condition;
    order->trigger = e.trigger;
}


void
SOB_CLASS::_insert_TRAILING_BRACKET_order(const order_queue_elem& e)
{
    assert( detail::order::is_trailing_bracket(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_nticks() );
    assert( e.cparams2 );
    assert( e.cparams2->is_by_nticks() );

    /* if we fill immediately we need to insert other order from here */
    if( _inject_basic_order(e, detail::order::needs_partial_fill(e)) )
    {
        _exec_TRAILING_BRACKET_order(*e.cparams1, *e.cparams2, e.cb,
                                     e.trigger, e.id);
        /*
         * TODO ID replace like OCO ?
         */
        return;
    }

    auto& order = _id_cache.at(e.id);
    assert( order );

    order->nticks_bracket_orders = new nticks_bracket_type(
            reinterpret_cast<const OrderParamatersByNTicks&>(*e.cparams1),
            reinterpret_cast<const OrderParamatersByNTicks&>(*e.cparams2)
            );
    order->cond = e.condition;
    order->trigger = e.trigger;
}


void
SOB_CLASS::_insert_TRAILING_STOP_order(const order_queue_elem& e)
{
    assert( detail::order::is_trailing_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_nticks() );

    if( _inject_basic_order(e, detail::order::needs_partial_fill(e)) )
    {
        _exec_TRAILING_STOP_order(*e.cparams1, e.cb, e.trigger, e.id);
        return;
    }

    auto& order = _id_cache.at(e.id);
    assert( order );

    order->contingent_order = e.cparams1->copy_new().release(); // TODO
    order->cond = e.condition;
    order->trigger = e.trigger;
}


void
SOB_CLASS::_insert_TRAILING_BRACKET_ACTIVE_order(const order_queue_elem& e)
{
    using namespace detail;

    assert( order::is_active_trailing_bracket(e) );
    assert( order::is_limit(e) || order::is_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->get_order_type() != order_type::market );
    assert( e.cparams1->get_order_type() != order_type::null );
    assert( e.cparams1->is_by_nticks() );

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_basic_order(e, order::needs_partial_fill(e)) )
    {
        _push_exec_callback( callback_msg::trigger_BRACKET_close, e.cb,
                             e.id, e.id, 0, 0 );
        return;
    }

    /* find the relevant order previously injected */
    auto& order1 = _id_cache.at(e.id);
    assert( order1 );

    id_type id2 = _generate_id();
    size_t nticks = e.cparams1->stop_nticks();
    plevel p = _trailing_stop_plevel( e.cparams1->is_buy(), nticks );

    stop_bndl order2( e.cparams1->is_buy(), 0, id2, e.cparams1->size(),
                      e.cb, order_condition::_trailing_bracket_active,
                      e.trigger );

    /* link each order with the other */
    order1->linked_trailer = _new_linked_trailer(0, false, _itop(p), id2, false);
    order2.linked_trailer = _new_linked_trailer(nticks, e, true);

    /* transfer condition/trigger info */
    order1->cond = order2.cond = e.condition;
    order1->trigger = order2.trigger = e.trigger;

    /* make the new stop bndl active */
    chain<stop_chain_type>::push(this, p, std::move(order2));
    _trailing_stop_insert( id2, e.cparams1->is_buy() );
}


void
SOB_CLASS::_insert_TRAILING_STOP_ACTIVE_order(const order_queue_elem& e)
{
    assert( detail::order::is_active_trailing_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_nticks() );

    stop_bndl bndl(e.is_buy, 0, e.id, e.sz, e.cb, e.condition, e.trigger);
    bndl.nticks = e.cparams1->stop_nticks();

    plevel p = _trailing_stop_plevel( e.cparams1->is_buy(), bndl.nticks );

    /* make the new stop bndl active */
    detail::chain<stop_chain_type>::push(this, p, std::move(bndl));
    _trailing_stop_insert(e.id, e.is_buy);
}


void
SOB_CLASS::_insert_FOK_order(const order_queue_elem& e)
{
    assert( detail::order::is_limit(e) );
    plevel p = _ptoi(e.limit);

    bool allow_partial = detail::order::needs_partial_fill(e);
    bool fillable = e.is_buy
        ? _limit_is_fillable<true>(p, e.sz, allow_partial ).first
        : _limit_is_fillable<false>(p, e.sz, allow_partial ).first;

    if( !fillable ){
        _push_exec_callback(callback_msg::kill, e.cb, e.id, e.id, e.limit, e.sz);
        return;
    }

#ifdef NDEBUG
    _route_basic_order(e);
#else
    fill_type ftype = _route_basic_order(e);
    assert( ftype != fill_type::none );
#endif /* NDEBUG */
}


void
SOB_CLASS::_insert_ALL_OR_NOTHING_order(const order_queue_elem& e)
{
    /* essentially a limit order */
    assert( detail::order::is_limit(e) );
#ifdef NDEBUG
    _route_basic_order(e, true);
#else
    fill_type ftype = _route_basic_order(e, true);
    assert( ftype == fill_type::immediate_full || ftype == fill_type::none );
#endif /* NDEBUG */
}


void
SOB_CLASS::_trailing_stop_insert(id_type id, bool is_buy)
{
    auto& stops = is_buy ? _trailing_buy_stops : _trailing_sell_stops;
    stops.insert(id);
}


void
SOB_CLASS::_trailing_stop_erase(id_type id, bool is_buy)
{
    auto& stops = is_buy ? _trailing_buy_stops : _trailing_sell_stops;
    stops.erase(id);
}


SOB_CLASS::plevel
SOB_CLASS::_trailing_stop_plevel(bool buy_stop, size_t nticks)
{
    return nticks ? (_last + (buy_stop ? nticks : (nticks*-1))) : 0;
}


SOB_CLASS::plevel
SOB_CLASS::_trailing_limit_plevel(bool buy_limit, size_t nticks)
{
    return nticks ? (_last + (buy_limit ? (nticks*-1) : nticks)) : 0;
}


void
SOB_CLASS::_trailing_stops_adjust(bool buy_stops)
{
    auto& stops = buy_stops ? _trailing_buy_stops : _trailing_sell_stops;
    for( auto id : stops )
        _trailing_stop_adjust(id, buy_stops);
}


void
SOB_CLASS::_trailing_stop_adjust(id_type id, bool buy_stop)
{
    using namespace detail;

    /* (temporarily) remove active stop */
    stop_bndl bndl = chain<stop_chain_type>::pop(this, id);
    assert( bndl );
    assert( bndl.nticks );
    assert( bndl.is_buy == buy_stop );

    bool is_ats = order::is_active_trailing_stop(bndl);
    bool is_atb = order::is_active_trailing_bracket(bndl);
    assert( is_ats || is_atb );

    size_t nticks = is_ats ? bndl.nticks : bndl.linked_trailer->first;
    plevel p = _trailing_stop_plevel(buy_stop, nticks);
    double price = _itop(p);

    _push_exec_callback( callback_msg::adjust_trailing_stop, bndl.cb, id, id,
                         price, bndl.sz );

    /* if bracket, need to let linked order know new location */
    if( is_atb ){
        assert( bndl.linked_trailer );
        const order_location& loc = bndl.linked_trailer->second;
        auto& linked = _id_cache.at(loc.id);
        assert( order::is_active_trailing_bracket(*linked) );
        linked->linked_trailer->second.price = price;
    }

    /* make 'new' stop active again */
    chain<stop_chain_type>::push(this, p, std::move(bndl));
}


template<typename... Args>
SOB_CLASS::linked_trailer_type*
SOB_CLASS::_new_linked_trailer(size_t nticks, Args&&... args) const
{
    return new linked_trailer_type(
        nticks, order_location(std::forward<Args>(args)...)
        );
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
    case order_condition::all_or_nothing: /* no break */
    case order_condition::none:
        break;
    };

    if( loc ){
        /* reconstruct OrderParamaters from order_location */
        aot.change_order1(
            loc->is_limit_chain
               ? detail::order::template
                 as_price_params<limit_chain_type>(this, loc->id)
               : detail::order::template
                 as_price_params<stop_chain_type>(this, loc->id)
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
    case order_condition::all_or_nothing:
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

    long long ticks = _ticks_in_range(_itop(_beg), _itop(_end-1));

    if( static_cast<long>(order->limit_nticks()) > ticks ){
        throw advanced_order_error("limit_nticks too large");
    }
    if( static_cast<long>(order->stop_nticks()) > ticks ){
        throw advanced_order_error("stop_nticks too large");
    }

    return std::unique_ptr<OrderParamaters>(
            new OrderParamatersByNTicks(
                buy, size, order->limit_nticks(), order->stop_nticks()
                )
    );
}


std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_price_params(const OrderParamaters *order) const
{
    assert( order->is_by_price() );

    if( !order->size() )
        throw advanced_order_error("invalid order size");

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
            new OrderParamatersByPrice(
                order->is_buy(), order->size(), limit, stop
                )
    );
}


void
SOB_CLASS::_check_limit_order( bool buy,
                               double limit,
                               std::unique_ptr<OrderParamaters>& op,
                               order_condition oc) const
{
    assert( op->is_by_price() );

    if( !op->size() )
        throw advanced_order_error(oc + " invalid order size");

    order_type ot = op->get_order_type();
    if( ot == order_type::market )
        throw advanced_order_error(oc + " limit/market not valid order type");
    else if( ot != order_type::limit )
        return;

    if( buy && !op->is_buy() && limit >= op->limit_price() )
        throw advanced_order_error(oc + " limit/limit buy price >= sell price");
    else if( !buy && op->is_buy() && limit <= op->limit_price() )
        throw advanced_order_error(oc + " limit/limit sell price <= buy price");
    else if( op->limit_price() == limit )
         throw advanced_order_error(oc + " limit/limit of same price" );
}

}; /* sob */

#undef SOB_CLASS
