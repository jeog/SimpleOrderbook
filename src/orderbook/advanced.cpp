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
    case order_condition::_trailing_bracket_active:
        _insert_active_bracket_order<true>(e);
        break;
    case order_condition::_bracket_active:
        _insert_active_bracket_order<false>(e);
        break;
    case order_condition::one_cancels_other:
        _insert_OCO_order(e);
        break;
    case order_condition::trailing_stop:
        _insert_TRAILING_STOP_order(e);
        break;
    case order_condition::trailing_bracket:
        _insert_bracket_order<true>(e);
        break;
    case order_condition::bracket:
        _insert_bracket_order<false>(e);
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
    case order_condition::all_or_none:
        _insert_ALL_OR_NONE_order(e);
        break;
    default:
        throw std::runtime_error("invalid advanced order condition");
    }
}


bool
SOB_CLASS::_handle_advanced_order_trigger(_order_bndl& bndl, id_type id, size_t sz)
{
    switch(bndl.condition){
    case order_condition::one_cancels_other:
    case order_condition::all_or_none:
        return false; /* NO OP */
    case order_condition::_trailing_stop_active:
        _handle_TRAILING_STOP_ACTIVE(bndl, id, sz);
        break;
    case order_condition::_trailing_bracket_active:
        _handle_active_bracket<true>(bndl, id, sz);
        break;
    case order_condition::_bracket_active:
        _handle_active_bracket<false>(bndl, id, sz);
        break;
    case order_condition::one_triggers_other:
        _handle_OTO(bndl, id, sz);
        break;
    case order_condition::trailing_bracket:
        _handle_bracket<true>(bndl, id, sz);
        break;
    case order_condition::bracket:
        _handle_bracket<false>(bndl, id, sz);
        break;
    case order_condition::trailing_stop:
        _handle_TRAILING_STOP(bndl, id, sz);
        break;
    default:
        throw std::runtime_error("invalid order condition");
    }
    return true;
}


bool
SOB_CLASS::_handle_advanced_order_cancel(_order_bndl& bndl, id_type id, size_t sz)
{
    switch(bndl.condition){
    case order_condition::one_triggers_other:
    case order_condition::bracket:
    case order_condition::trailing_bracket:
    case order_condition::trailing_stop:
    case order_condition::_trailing_stop_active:
    case order_condition::all_or_none:
        return false; /* NO OP */
        /* no break */
    case order_condition::_trailing_bracket_active: /* no break */
    case order_condition::_bracket_active:
        if( sz != bndl.sz )
            return false;
        /* no break */
    case order_condition::one_cancels_other:
        _handle_OCO(bndl, id, sz);
        return true;
    default:
        throw std::runtime_error("invalid order condition");
    }
}


void
SOB_CLASS::_handle_OTO(_order_bndl& bndl, id_type id, size_t sz)
{
    assert( bndl.contingent_price_order );

    _exec_OTO_order( bndl.contingent_price_order->params, bndl.cb, id);

    delete bndl.contingent_price_order;
    bndl.contingent_price_order = nullptr;
    bndl.condition = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


void
SOB_CLASS::_handle_OCO(_order_bndl& bndl, id_type id, size_t sz)
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

    _exec_OCO_order( bndl, (loc->is_primary ? loc->id : id), id, loc->id );

    /* remove linked order from union */
    if( is_atb ){
        delete bndl.linked_trailer;
        bndl.linked_trailer = nullptr;
    }else{
        delete bndl.linked_order;
        bndl.linked_order = nullptr;
    }
    bndl.condition = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


template<bool IsTrailing>
void
SOB_CLASS::_handle_bracket(_order_bndl& bndl, id_type id, size_t sz)
{
    assert( bndl.price_bracket_orders );
    assert( bndl.price_bracket_orders->first.is_stop_order() );
    assert( bndl.price_bracket_orders->second.is_limit_order() );

    bndl.price_bracket_orders->first.change_size(sz);
    bndl.price_bracket_orders->second.change_size(sz);

    bool exec_bracket = true;
    if( bndl.price_bracket_orders->active1 ){
        assert( bndl.price_bracket_orders->active2);
        /*
         * if we've already executed the order that issues the bracket but
         * now have a second fill that needs to update linked bracket orders
         */
        try{
            auto& iwrap1 = _from_cache(bndl.price_bracket_orders->active1);
            auto& iwrap2 = _from_cache(bndl.price_bracket_orders->active2);

            iwrap1.incr_size(sz);
            _push_exec_callback( callback_msg::trigger_BRACKET_adj_loss,
                                 iwrap1->cb, iwrap1->id, iwrap1->id,
                                 _itop(iwrap1.p), iwrap1->sz);

            iwrap2.incr_size(sz);
            _push_exec_callback( callback_msg::trigger_BRACKET_adj_target,
                                 iwrap2->cb, iwrap2->id, iwrap2->id,
                                 _itop(iwrap2.p), iwrap2->sz);

            exec_bracket = false;
        }catch(OrderNotInCache&){
            assert( !_in_cache(bndl.price_bracket_orders->active1) );
            assert( !_in_cache(bndl.price_bracket_orders->active2) );
        }
    }

    if( exec_bracket ){
        _exec_bracket_order<IsTrailing>( bndl.price_bracket_orders->first,
                                         bndl.price_bracket_orders->second,
                                         sz, bndl.cb,
                                         condition_trigger::fill_partial, id );
    }

    if( sz == bndl.sz ){
        /*
         * delete what bracket_orders points at; the order is now an
         * 'active' condition
         */
        if( IsTrailing ){
            delete bndl.nticks_bracket_orders;
            bndl.nticks_bracket_orders = nullptr;
        }else{
            delete bndl.price_bracket_orders;
            bndl.price_bracket_orders = nullptr;
        }
    }
}


template<bool IsTrailing>
void
SOB_CLASS::_handle_active_bracket(_order_bndl& bndl, id_type id, size_t sz)
{
    using namespace detail;

    if( sz == bndl.sz )
        return;

    id_type other_id;
    if( IsTrailing ){
        assert( order::is_active_trailing_bracket(bndl) );
        assert( bndl.linked_trailer );
        other_id = bndl.linked_trailer->second.id;
    }else{
        assert( order::is_active_bracket(bndl) );
        assert( bndl.linked_order );
        other_id = bndl.linked_order->id;
    }

    /* SHOULDN'T THROW */
    auto& iwrap = _from_cache(other_id);
    iwrap.decr_size(sz);

    auto msg = iwrap.is_limit()
            ? callback_msg::trigger_BRACKET_adj_target
            : callback_msg::trigger_BRACKET_adj_loss;

    _push_exec_callback(msg, iwrap->cb, other_id, other_id, _itop(iwrap.p),
                        iwrap->sz);
}


void
SOB_CLASS::_handle_TRAILING_STOP(_order_bndl& bndl, id_type id, size_t sz)
{
    assert( bndl.contingent_nticks_order);
    assert( bndl.contingent_nticks_order->params.is_stop_order() );

    bndl.contingent_nticks_order->params.change_size(sz);

    bool exec_bracket = true;
    if( bndl.contingent_nticks_order->active ){
        /*
         * if we've already executed the order that issues the trailing stop
         * but now have a second fill that needs to update contingent order
         */
        try{
            auto& iwrap = _from_cache(bndl.contingent_nticks_order->active);
            iwrap.incr_size(sz);
            _push_exec_callback( callback_msg::trigger_TRAILING_STOP_adj_loss,
                                 iwrap->cb, iwrap->id, iwrap->id,
                                 _itop(iwrap.p), iwrap->sz );
            exec_bracket = false;
        }catch(OrderNotInCache&){
        }
    }

    if( exec_bracket ){
        _exec_TRAILING_STOP_order( bndl.contingent_nticks_order->params, sz,
                                   bndl.cb, condition_trigger::fill_partial, id);
    }

    if( sz == bndl.sz ){
        delete bndl.contingent_nticks_order;
        bndl.contingent_nticks_order = nullptr;
    }
}


void
SOB_CLASS::_handle_TRAILING_STOP_ACTIVE(_order_bndl& bndl, id_type id, size_t sz)
{
    assert( detail::order::is_active_trailing_stop(bndl) );
    assert( sz == bndl.sz );

    _push_exec_callback(callback_msg::trigger_TRAILING_STOP_close,
                        bndl.cb, id, id, 0, 0);
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


template<typename T>
void
SOB_CLASS::_exec_OCO_order( const T& t,
                            id_type id_old,
                            id_type id_new,
                            id_type id_pull )
{
    switch( t.condition ){
    case order_condition::one_cancels_other:
        _push_exec_callback(callback_msg::trigger_OCO, t.cb, id_old, id_new, 0, 0);
        break;
    case order_condition::_bracket_active: /* no break */
    case order_condition::_trailing_bracket_active:
        _push_exec_callback( callback_msg::trigger_BRACKET_close, t.cb, id_old,
                             id_new, 0, 0 );
        break;
    default:
        throw std::runtime_error("invalid condition in exec_OCO");
    }

    if( id_pull ){ /* remove primary order, BE SURE pull_linked=false */
        _pull_order(id_pull, false);
    }
}


template<bool IsTrailing>
void
SOB_CLASS::_exec_bracket_order( const OrderParamaters& op1,
                                const OrderParamaters& op2,
                                size_t sz,
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
    assert( op2.get_order_type() == order_type::limit );
    assert( op2.stop_price() == 0 );

    if( IsTrailing ){
        assert( op1.is_by_nticks() );
        assert( op2.is_by_nticks() );
    }else{
        assert( op1.is_by_price() );
        assert( op2.is_by_price() );
    }

    id_type id_new = _generate_id();
    _push_exec_callback(callback_msg::trigger_BRACKET_open, cb, id, id_new, 0, 0);

    double limit;
    order_condition oc;
    if( IsTrailing ){
        oc = order_condition::_trailing_bracket_active;
        plevel p = _trailing_limit_plevel( op2.is_buy(), op2.limit_nticks() );
        limit = _itop(p);
    }else{
        oc = order_condition::_bracket_active;
        limit = op2.limit_price();
    }

    auto stop_order = op1.copy_new();
    stop_order->change_size(sz);

    _push_internal_order( order_type::limit, op2.is_buy(), limit, 0, sz, cb, oc,
                          trigger, std::move(stop_order),  nullptr, id_new, id );

}


void
SOB_CLASS::_exec_TRAILING_STOP_order( const OrderParamaters& op,
                                      size_t sz,
                                      const order_exec_cb_bndl& cb,
                                      condition_trigger trigger,
                                      id_type id )
{
    assert( op.is_by_nticks() );

    id_type id_new = _generate_id();
    _push_exec_callback( callback_msg::trigger_TRAILING_STOP_open, cb,
                         id, id_new, 0, 0 );

    auto stop_order = op.copy_new();
    stop_order->change_size(sz);

    _push_internal_order( order_type::stop, op.is_buy(), 0, 0, sz, cb,
                          order_condition::_trailing_stop_active, trigger,
                          std::move(stop_order), nullptr, id_new, id );
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

    order->contingent_price_order = contingent_price_order_type::New(*e.cparams1);
    order->condition = e.condition;
    order->trigger = e.trigger;
}


void
SOB_CLASS::_insert_OCO_order(order_queue_elem& e)
{
    using namespace detail;

    assert( order::is_OCO(e) );
    assert( order::is_limit(e) || order::is_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->get_order_type() != order_type::market );
    assert( e.cparams1->get_order_type() != order_type::null );
    assert( e.cparams1->is_by_price() );

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_basic_order(e, order::needs_partial_fill(e)) )
    {
        _exec_OCO_order(e, e.id, e.id, 0 );
        return;
    }

    /* construct a new queue elem from cparams1, with a new ID. */
    id_type id2 = _generate_id();
    order_queue_elem e2( reinterpret_cast<OrderParamatersByPrice&>(*e.cparams1),
                         e.cb, id2, e.condition, e.trigger, nullptr, nullptr );

    /* if we fill second order immediately, remove first */
    if( _inject_basic_order(e2, order::needs_partial_fill(e)) )
    {
        _exec_OCO_order( e, e.id, id2, e.id );
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
    order1->condition = order2->condition = e.condition;
    order1->trigger = order2->trigger = e.trigger;
}



template<bool IsTrailing>
void
SOB_CLASS::_insert_bracket_order(const order_queue_elem& e)
{
    assert( e.cparams1 );
    assert( e.cparams2 );

    if( IsTrailing ){
        assert( detail::order::is_trailing_bracket(e) );
        assert( e.cparams1->is_by_nticks() );
        assert( e.cparams2->is_by_nticks() );
    }else{
        assert( detail::order::is_bracket(e) );
        assert( e.cparams1->is_by_price() );
        assert( e.cparams2->is_by_price() );
    }

    size_t filled = _route_basic_order<>(e, true);

    /* if we fill immediately we need to insert other orders from here */
    if( (detail::order::needs_partial_fill(e) && filled) || filled == e.sz )
    {
        _exec_bracket_order<IsTrailing>(*e.cparams1, *e.cparams2, filled, e.cb,
                                        condition_trigger::fill_partial, e.id);
        if( filled == e.sz )
            return;
    }

    auto& order = _id_cache.at(e.id);
    assert(order);

    auto cp1 = e.cparams1->copy_new();
    auto cp2 = e.cparams2->copy_new();
    cp1->change_size( cp1->size() - filled );
    cp2->change_size( cp2->size() - filled );

    if( IsTrailing ){
        order->nticks_bracket_orders = nticks_bracket_type::New(*cp1 ,*cp2);
    }else{
        order->price_bracket_orders = price_bracket_type::New(*cp1, *cp2);
    }

    order->condition = e.condition;
    order->trigger = e.trigger;

}


template<bool IsTrailing>
void
SOB_CLASS::_insert_active_bracket_order(const order_queue_elem& e)
{
    using namespace detail;

    assert( order::is_limit(e));
    assert( e.cparams1 );
    assert( e.cparams1->get_order_type() == order_type::stop
            || e.cparams1->get_order_type() == order_type::stop_limit );
    assert( e.trigger == condition_trigger::fill_partial );

    if( IsTrailing ){
        assert( order::is_active_trailing_bracket(e) );
        assert( e.cparams1->is_by_nticks() );
    }else{
        assert( order::is_active_bracket(e) );
        assert( e.cparams1->is_by_price() );
    }

    size_t rmndr = e.sz - _route_basic_order<>(e);

    /* if we fill (all) immediately, no need to enter 2nd order */
    if( rmndr == 0 ){
        _push_exec_callback(callback_msg::trigger_BRACKET_close,
                            e.cb, e.id, e.id, 0, 0);
        return;
    }

    /* signal ID of target side of bracket */
    _push_exec_callback(callback_msg::trigger_BRACKET_open_target, e.cb,
                        e.parent_id, e.id, e.limit, rmndr );

    plevel p;
    order_condition oc;
    size_t nticks = e.cparams1->stop_nticks();

    if( IsTrailing ){
        assert( nticks > 0 );
        oc = order_condition::_trailing_bracket_active;
        p = _trailing_stop_plevel( e.cparams1->is_buy(), nticks );
    }else{
        oc = order_condition::_bracket_active;
        p = _ptoi( e.cparams1->stop_price() );
    }

    /*
     * we don't need to insert/inject/route the loss order, just build it
     * here and after we link with the target order push directly to the
     * stop chain
     */
    id_type id2 = _generate_id();
    stop_bndl order2( e.cparams1->is_buy(), e.cparams1->limit_price(), id2,
                      rmndr, e.cb, oc, e.trigger );

    /* retrieve the target order inserted above */
    auto& order1 = _id_cache.at(e.id);
    assert(order1);

    /* link each order with the other */
    if( IsTrailing ){
        order1->linked_trailer =
            _new_linked_trailer(0, false, _itop(p), id2, false);
        order2.linked_trailer = _new_linked_trailer(nticks, e, true);
    }else{
        order1->linked_order =
            new order_location(false, e.cparams1->stop_price(), id2, false);
        order2.linked_order = new order_location(e, true);
    }

    /* transfer condition/trigger info */
    order1->condition = order2.condition = e.condition;
    order1->trigger = order2.trigger = e.trigger;

    /* push to the stop/loss order directly to the appropriate chain */
    chain<stop_chain_type>::push(this, p, std::move(order2));
    if( IsTrailing )
        _trailing_stop_insert( id2, e.cparams1->is_buy() );

    /* signal ID of stop/loss side of bracket */
    _push_exec_callback(callback_msg::trigger_BRACKET_open_loss, e.cb,
                        e.parent_id, id2, _itop(p), rmndr );


    /* find the entry order and let it know about us for dynamic updates */
    try{
        auto& bndl = *_from_cache(e.parent_id);
        if( IsTrailing )
            assert( order::is_trailing_bracket(bndl) );
        else
            assert( order::is_bracket(bndl) );
        bndl.nticks_bracket_orders->active1 = id2; // stop/loss first;
        bndl.nticks_bracket_orders->active2 = e.id; // target second
    }catch(OrderNotInCache&){
    }
}


void
SOB_CLASS::_insert_TRAILING_STOP_order(const order_queue_elem& e)
{
    assert( detail::order::is_trailing_stop(e) );
    assert( e.cparams1 );
    assert( e.cparams1->is_by_nticks() );

    size_t filled = _route_basic_order<>(e, true);

    /* if we fill immediately we need to insert other orders from here */
    if( (detail::order::needs_partial_fill(e) && filled ) || filled == e.sz )
    {
        _exec_TRAILING_STOP_order(*e.cparams1, filled, e.cb, e.trigger, e.id);
        if( filled == e.sz )
            // TODO if filled all return stop ID instead ??
            return;
    }

    auto& order = _id_cache.at(e.id);
    assert( order );

    auto cp1 = e.cparams1->copy_new();
    cp1->change_size( cp1->size() - filled );

    order->contingent_nticks_order = contingent_nticks_order_type::New(*cp1);
    order->condition = e.condition;
    order->trigger = e.trigger;
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

    _push_exec_callback( callback_msg::trigger_TRAILING_STOP_open_loss, e.cb,
                         e.parent_id, e.id, _itop(p), e.sz );

    /* find the entry order and let it know about us for dynamic updates */
    try{
        auto& bndl = *_from_cache(e.parent_id);
        assert( detail::order::is_trailing_stop(bndl) );
        bndl.contingent_nticks_order->active = e.id;
    }catch(OrderNotInCache&){
    }

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
    size_t filled = _route_basic_order(e);
    assert( filled > 0 );
#endif /* NDEBUG */
}


void
SOB_CLASS::_insert_ALL_OR_NONE_order(const order_queue_elem& e)
{
    /* essentially a limit order */
    assert( detail::order::is_limit(e) );
#ifdef NDEBUG
    _route_basic_order(e, true);
#else
    size_t filled = _route_basic_order(e, true);
    assert( filled == e.sz || filled == 0 );
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


template<bool IsStop>
SOB_CLASS::plevel
SOB_CLASS::_plevel_offset(bool buy, size_t nticks, plevel from) const
{
    assert( nticks > 0 );
    _assert_plevel( from );

    if( IsStop ? buy : !buy ){
        if( nticks > static_cast<size_t>(_end - 1 - from) ){
            std::stringstream ss;
            ss << nticks << " ticks from " << from << " is > " << _itop(_end - 1);
            throw derived_price_exception(ss.str());
        }
        return from + nticks;
    }else{
        if( nticks > static_cast<size_t>(from - _beg) ){
            std::stringstream ss;
            ss << nticks << " ticks from " << from << " is < " << _itop(_beg);
            throw derived_price_exception(ss.str());
        }
        return from + (nticks*-1);
    }
}


void
SOB_CLASS::_trailing_stops_adjust(bool buy_stops, plevel p)
{
    auto& stops = buy_stops ? _trailing_buy_stops : _trailing_sell_stops;
    for( auto id : stops )
        _trailing_stop_adjust(id, buy_stops, p);
}


void
SOB_CLASS::_trailing_stop_adjust(id_type id, bool buy_stop, plevel p)
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
    plevel p_adj = _plevel_offset<true>(buy_stop, nticks, p);
    double price = _itop(p_adj);

    auto msg = is_ats ? callback_msg::trigger_TRAILING_STOP_adj_loss
                      : callback_msg::trigger_BRACKET_adj_loss;
    _push_exec_callback( msg, bndl.cb, id, id, price, bndl.sz );

    /* if bracket, need to let linked order know new location */
    // TODO do we need this if we're using the cache ??
    if( is_atb ){
        assert( bndl.linked_trailer );
        const order_location& loc = bndl.linked_trailer->second;
        auto& linked = _id_cache.at(loc.id);
        assert( order::is_active_trailing_bracket(*linked) );
        linked->linked_trailer->second.price = price;
    }

    /* make 'new' stop active again */
    chain<stop_chain_type>::push(this, p_adj, std::move(bndl));
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
    aot.change_condition(bndl.condition);
    aot.change_trigger(bndl.trigger);

    order_location *loc = nullptr;

    switch( bndl.condition ){
    case order_condition::_bracket_active: /* no break */
    case order_condition::one_cancels_other:
        loc = bndl.linked_order;
        break;
    case order_condition::_trailing_bracket_active:
        loc = &(bndl.linked_trailer->second);
        break;
    case order_condition::trailing_stop:
        aot.change_order1( bndl.contingent_nticks_order->params );
        break;
    case order_condition::one_triggers_other:
        aot.change_order1( bndl.contingent_price_order->params );
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
        // TODO does active TS need to convert 'nticks' to Order Params ??
    case order_condition::all_or_none: /* no break */
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
        pp1 = _build_price_params(size, advanced.order1());
        pp2 = _build_price_params(size, advanced.order2());
        break;
    case order_condition::one_triggers_other: /* no break */
    case order_condition::one_cancels_other:
        pp1 = _build_price_params(advanced.order1()->size(), advanced.order1());
        break;
    case order_condition::fill_or_kill:
    case order_condition::all_or_none:
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
SOB_CLASS::_build_price_params(size_t size, const OrderParamaters *order) const
{
    assert( order->is_by_price() );

    if( size == 0 )
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
                order->is_buy(), size, limit, stop
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

void
SOB_CLASS::_check_nticks( bool above, double limit, size_t nticks ) const
{
    if( nticks == 0 )
        throw advanced_order_error("nticks can not be 0");

    plevel p = _ptoi(limit);
    _assert_plevel(p);

    // assert_plevel insures we don't wrap here
    size_t avail = above ? _end - 1 - p : p - _beg;

    if( nticks > avail )
        throw advanced_order_error(
                "nticks would create order outside of tradable range"
                );
}

}; /* sob */

#undef SOB_CLASS
