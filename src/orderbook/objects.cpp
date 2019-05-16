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

namespace sob{

typename SimpleOrderbook::SimpleOrderbookBase::limit_bndl
SimpleOrderbook::SimpleOrderbookBase::limit_bndl::null;

typename SimpleOrderbook::SimpleOrderbookBase::stop_bndl
SimpleOrderbook::SimpleOrderbookBase::stop_bndl::null;

SOB_CLASS::_order_bndl::_order_bndl()
     :
        _order_bndl(0, 0, {nullptr, order_exec_cb_bndl::type::synchronous})
     {
     }


SOB_CLASS::_order_bndl::_order_bndl( id_type id,
                                     size_t sz,
                                     const order_exec_cb_bndl& cb,
                                     order_condition cond,
                                     condition_trigger trigger )
    :
        id(id),
        sz(sz),
        cb(cb),
        cond(cond),
        trigger(trigger),
        nticks(0)
    {
    }



SOB_CLASS::_order_bndl::_order_bndl(const _order_bndl& bndl)
    :
        id(bndl.id),
        sz(bndl.sz),
        cb(bndl.cb),
        cond(bndl.cond),
        trigger(bndl.trigger)
    {
        switch(cond){
        case order_condition::_bracket_active: /* no break */
        case order_condition::one_cancels_other:
            linked_order = bndl.linked_order
                         ? new order_location(*bndl.linked_order)
                         : nullptr;
            break;
        case order_condition::trailing_stop: /* no break */
        case order_condition::one_triggers_other:
            contingent_order = bndl.contingent_order
                             ? bndl.contingent_order->copy_new().release() // TODO
                             : nullptr;
            break;
        case order_condition::trailing_bracket:
            nticks_bracket_orders = bndl.nticks_bracket_orders
                          ? new nticks_bracket_type(*bndl.nticks_bracket_orders)
                          : nullptr;
            break;
        case order_condition::bracket:
            price_bracket_orders = bndl.price_bracket_orders
                          ? new price_bracket_type(*bndl.price_bracket_orders)
                          : nullptr;
            break;
        case order_condition::_trailing_stop_active:
            nticks = bndl.nticks;
            break;
        case order_condition::_trailing_bracket_active:
            linked_trailer = bndl.linked_trailer
                           ? new linked_trailer_type(*bndl.linked_trailer)
                           : nullptr;
            break;
        case order_condition::all_or_nothing: /* no break */
        case order_condition::fill_or_kill: /* no break */
        case order_condition::none:
            break;
        default:
            throw std::runtime_error("invalid order condition");
        }
    }



SOB_CLASS::_order_bndl::_order_bndl(_order_bndl&& bndl)
    :
        id(bndl.id),
        sz(bndl.sz),
        cb(bndl.cb),
        cond(bndl.cond),
        trigger(bndl.trigger)
    {
        switch(cond){
        case order_condition::_bracket_active: /* break */
        case order_condition::one_cancels_other:
            linked_order = bndl.linked_order;
            bndl.linked_order = nullptr;
            break;
        case order_condition::trailing_stop: /* no break */
        case order_condition::one_triggers_other:
            contingent_order = bndl.contingent_order;
            bndl.contingent_order = nullptr;
            break;
        case order_condition::trailing_bracket:
            nticks_bracket_orders = bndl.nticks_bracket_orders;
            bndl.nticks_bracket_orders = nullptr;
            break;
        case order_condition::bracket:
            price_bracket_orders = bndl.price_bracket_orders;
            bndl.price_bracket_orders = nullptr;
            break;
        case order_condition::_trailing_stop_active:
            nticks = bndl.nticks;
            break;
        case order_condition::_trailing_bracket_active:
            linked_trailer = bndl.linked_trailer;
            bndl.linked_trailer = nullptr;
            break;
        case order_condition::all_or_nothing: /* no break */
        case order_condition::fill_or_kill: /* no break */
        case order_condition::none:
            break;
        default:
            throw std::runtime_error("invalid order condition");
        };
    }


SOB_CLASS::_order_bndl::~_order_bndl()
   {
       switch(cond){
       case order_condition::_bracket_active: /* no break */
       case order_condition::one_cancels_other:
           if( linked_order )
               delete linked_order;
           break;
       case order_condition::trailing_stop: /* no break */
       case order_condition::one_triggers_other:
           if( contingent_order )
               delete contingent_order;
           break;
       case order_condition::trailing_bracket:
           if( nticks_bracket_orders )
               delete nticks_bracket_orders;
           break;
       case order_condition::bracket:
           if( price_bracket_orders )
               delete price_bracket_orders;
           break;
       case order_condition::_trailing_bracket_active:
           if( linked_trailer )
               delete linked_trailer;
           break;
       case order_condition::_trailing_stop_active: /* no break */
       case order_condition::all_or_nothing: /* no break */
       case order_condition::fill_or_kill: /* no break */
       case order_condition::none:
           break;
       default:
           std::cerr<< "invalid order condition in ~_order_bndl()" << std::endl;
       }
   }


SOB_CLASS::stop_bndl::stop_bndl()
    :
        _order_bndl(),
        is_buy(),
        limit()
    {
    }


SOB_CLASS::stop_bndl::stop_bndl( bool is_buy,
                                 double limit,
                                 id_type id,
                                 size_t sz,
                                 const order_exec_cb_bndl& cb,
                                 order_condition cond,
                                 condition_trigger trigger )
   :
       _order_bndl(id, sz, cb, cond, trigger),
       is_buy(is_buy),
       limit(limit)
   {
   }


SOB_CLASS::stop_bndl::stop_bndl(const stop_bndl& bndl)
   :
       _order_bndl(bndl),
       is_buy(bndl.is_buy),
       limit(bndl.limit)
   {
   }


SOB_CLASS::stop_bndl::stop_bndl(stop_bndl&& bndl)
   :
        _order_bndl(std::move(bndl)),
        is_buy(bndl.is_buy),
        limit(bndl.limit)
   {
   }


SOB_CLASS::order_location::order_location(const order_queue_elem& elem,
                                          bool is_primary)
    :
        is_limit_chain(elem.type == order_type::limit),
        price(is_limit_chain ? elem.limit : elem.stop),
        id(elem.id),
        is_primary(is_primary)
    {
    }


SOB_CLASS::order_location::order_location(bool is_limit,
                                          double price,
                                          id_type id,
                                          bool is_primary)
    :
        is_limit_chain(is_limit),
        price(price),
        id(id),
        is_primary(is_primary)
    {
    }


SOB_CLASS::level::level()
    :
        _l_chain(new limit_chain_type()),
        _s_chain(new stop_chain_type()),
        _aon_b_chain(nullptr),
        _aon_s_chain(nullptr)
    {}


template<bool BuyChain>
SOB_CLASS::aon_chain_type*
SOB_CLASS::level::get_aon_chain() const
{
    return BuyChain ? _aon_b_chain.get() : _aon_s_chain.get();
}
template SOB_CLASS::aon_chain_type* SOB_CLASS::level::get_aon_chain<true>() const;
template SOB_CLASS::aon_chain_type* SOB_CLASS::level::get_aon_chain<false>() const;


template<bool BuyChain>
bool
SOB_CLASS::level::aon_chain_is_empty() const
{
    auto& uptr = BuyChain ? _aon_b_chain : _aon_s_chain;
    if( !uptr )
        return true;

    assert( !uptr->empty() );  // if empty should be null
    return false;
}
template bool SOB_CLASS::level::aon_chain_is_empty<true>() const;
template bool SOB_CLASS::level::aon_chain_is_empty<false>() const;


template<bool BuyChain>
void
SOB_CLASS::level::create_aon_chain()
{
    auto& uptr = BuyChain ? _aon_b_chain : _aon_s_chain;
    assert( !uptr );
    uptr.reset( new aon_chain_type() );
}
template void SOB_CLASS::level::create_aon_chain<true>();
template void SOB_CLASS::level::create_aon_chain<false>();


template<bool BuyChain>
void
SOB_CLASS::level::destroy_aon_chain()
{
    auto& uptr = BuyChain ? _aon_b_chain : _aon_s_chain;
    assert( uptr );
    uptr.reset();
}
template void SOB_CLASS::level::destroy_aon_chain<true>();
template void SOB_CLASS::level::destroy_aon_chain<false>();


template<bool BuyChain>
SOB_CLASS::aon_chain_type::iterator
SOB_CLASS::level::push_aon_bndl(aon_bndl&& bndl)
{
    auto& uptr = BuyChain ? _aon_b_chain : _aon_s_chain;
    if( !uptr )
        create_aon_chain<BuyChain>();
    uptr->push_back( std::move(bndl) );
    return --(uptr->end());
}
template SOB_CLASS::aon_chain_type::iterator
SOB_CLASS::level::push_aon_bndl<true>(aon_bndl&& );
template SOB_CLASS::aon_chain_type::iterator
SOB_CLASS::level::push_aon_bndl<false>(aon_bndl&& );


SOB_CLASS::chain_iter_wrap::chain_iter_wrap(
        limit_chain_type::iterator iter,
        plevel p
        )
    :
        l_iter(iter),
        type(itype::limit), p(p)
    {}

SOB_CLASS::chain_iter_wrap::chain_iter_wrap(
        stop_chain_type::iterator iter,
        plevel p
        )
    :
        s_iter(iter),
        type(itype::stop), p(p)
    {}

SOB_CLASS::chain_iter_wrap::chain_iter_wrap(
        aon_chain_type::iterator iter,
        plevel p,
        bool is_buy
        )
    :
        a_iter(iter),
        type( is_buy ? itype::aon_buy : itype::aon_sell ),
        p(p)
    {}

SOB_CLASS::_order_bndl&
SOB_CLASS::chain_iter_wrap::_get_base_bndl() const
{
    switch( type ){
    case chain_iter_wrap::itype::limit: return *l_iter;
    case chain_iter_wrap::itype::stop: return *s_iter;
    case chain_iter_wrap::itype::aon_buy: /* no break */
    case chain_iter_wrap::itype::aon_sell: return *a_iter;
    default:
        throw std::runtime_error("invalid chain_iter_wrap.itype");
    }
}


SOB_CLASS::OrderNotInCache::OrderNotInCache(id_type id)
    :
        std::logic_error("order #" + std::to_string(id)
                         + " not in cache")
    {}


SOB_CLASS::order_queue_elem_base_::order_queue_elem_base_(
        order_type ot,
        bool is_buy,
        double limit,
        double stop,
        size_t sz,
        order_exec_cb_bndl cb,
        id_type id )
    :
        type(ot),
        is_buy(is_buy),
        limit(limit),
        stop(stop),
        sz(sz),
        cb( cb ),
        id(id)
     {}

SOB_CLASS::order_queue_elem_base_::order_queue_elem_base_()
    :
        order_queue_elem_base_( order_type::null, false, 0, 0, 0,
                                {nullptr, order_exec_cb_bndl::type::synchronous},
                                0 )
    {}



SOB_CLASS::external_order_queue_elem::external_order_queue_elem(
        order_type ot,
        bool is_buy,
        double limit,
        double stop,
        size_t sz,
        order_exec_cb_bndl cb,
        id_type id,
        const AdvancedOrderTicket &aot,
        std::promise<id_type>&& promise )
    :
        order_queue_elem_base_(ot, is_buy, limit, stop, sz, cb, id),
        aot(aot),
        promise_async( std::move(promise) )
    {
        assert( cb.cb_type == order_exec_cb_bndl::type::asynchronous );
    }

SOB_CLASS::external_order_queue_elem::external_order_queue_elem(
      order_type ot,
      bool is_buy,
      double limit,
      double stop,
      size_t sz,
      order_exec_cb_bndl cb,
      id_type id,
      const AdvancedOrderTicket& aot,
      std::promise<std::pair<id_type, callback_queue_type>>&& promise
      )
    :
        order_queue_elem_base_(ot, is_buy, limit, stop, sz, cb, id),
        aot(aot),
        promise_sync( std::move(promise) )
    {
        assert( cb.cb_type == order_exec_cb_bndl::type::synchronous );
    }

SOB_CLASS::external_order_queue_elem::external_order_queue_elem()
    :
        order_queue_elem_base_(),
        aot(),
        promise_sync()
    {}


SOB_CLASS::external_order_queue_elem&
SOB_CLASS::external_order_queue_elem::operator=(
    external_order_queue_elem&& elem
    )
{
    if( cb.is_asynchronous() )
        promise_async.~promise();
    else
        promise_sync.~promise();

    switch( elem.cb.cb_type ){ // from type
    case order_exec_cb_bndl::type::synchronous:
        new (&promise_sync)
            std::promise<std::pair<id_type, callback_queue_type>>(
                std::move(elem.promise_sync)
            );
        break;
    case order_exec_cb_bndl::type::asynchronous:
        new (&promise_async)
            std::promise<id_type>(std::move(elem.promise_async));
        break;
    };

    order_queue_elem_base_::operator=( std::move(elem) );
    aot = std::move(elem.aot);
    return *this;
}


SOB_CLASS::external_order_queue_elem::~external_order_queue_elem()
    {
        switch( cb.cb_type ){
        case order_exec_cb_bndl::type::synchronous:
            promise_sync.~promise();
            break;
        case order_exec_cb_bndl::type::asynchronous:
            promise_async.~promise();
            break;
        };
    }


SOB_CLASS::order_queue_elem::order_queue_elem(
        order_type ot,
        bool is_buy,
        double limit,
        double stop,
        size_t sz,
        order_exec_cb_bndl cb,
        id_type id,
        order_condition condition,
        condition_trigger trigger,
        std::unique_ptr<OrderParamaters>&& cparams1,
        std::unique_ptr<OrderParamaters>&& cparams2 )
    :
        order_queue_elem_base_(ot, is_buy, limit, stop, sz, cb, id),
        condition(condition),
        trigger(trigger),
        cparams1( std::move(cparams1) ),
        cparams2( std::move(cparams2) )
    {}


SOB_CLASS::order_queue_elem::order_queue_elem(
        const external_order_queue_elem& e,
        const SOB_CLASS* sob )
    :
        order_queue_elem_base_(e.type, e.is_buy, e.limit, e.stop,
                               e.sz, e.cb, e.id),
        condition( e.aot.condition() ),
        trigger( e.aot.trigger() ),
        cparams1(),
        cparams2()
    {
        switch( type ){
        case order_type::market:
            if( e.aot )
                std::tie(cparams1, cparams2) = sob->_build_advanced_params(
                    is_buy, sz, e.aot);
            break;

        case order_type::limit:
            limit = sob->_tick_price_or_throw(limit, "invalid limit price");
            if( e.aot ){
                std::tie(cparams1, cparams2) = sob->_build_advanced_params(
                    is_buy, sz, e.aot);
                switch( condition ){
                case order_condition::bracket:
                    sob->_check_limit_order(is_buy, limit, cparams2, condition );
                    break;
                case order_condition::one_cancels_other:
                    sob->_check_limit_order(is_buy, limit, cparams1, condition );
                    break;
                default: break;
                };
            }
            break;

        case order_type::stop_limit:
            limit = sob->_tick_price_or_throw(limit, "invalid limit price");
            /* no break */
        case order_type::stop:
            stop = sob->_tick_price_or_throw(stop, "invalid stop price");
            if( e.aot ){
                std::tie(cparams1, cparams2) = sob->_build_advanced_params(
                    is_buy, sz, e.aot);
                if( cparams1->stop_price() == stop )
                    throw advanced_order_error("stop orders of same price");
            }
            break;

        default:
            throw std::runtime_error("invalid order type");
        }
    }


};


#undef SOB_CLASS



