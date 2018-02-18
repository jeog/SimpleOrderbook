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

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl()
     :
        _order_bndl(0, 0, nullptr)
     {
     }

SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl( id_type id,
                                     size_t sz,
                                     order_exec_cb_type exec_cb,
                                     order_condition cond,
                                     condition_trigger trigger )
    :
        id(id),
        sz(sz),
        exec_cb(exec_cb),
        cond(cond),
        trigger(trigger),
        nticks(0)
    {
    }

SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl(const _order_bndl& bndl)
    :
        id(bndl.id),
        sz(bndl.sz),
        exec_cb(bndl.exec_cb),
        cond(bndl.cond),
        trigger(bndl.trigger)
    {
        _copy_union(bndl);
    }


SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl(_order_bndl&& bndl)
    :
        id(bndl.id),
        sz(bndl.sz),
        exec_cb(bndl.exec_cb),
        cond(bndl.cond),
        trigger(bndl.trigger)
    {
        _move_union(bndl);
    }


SOB_TEMPLATE
typename SOB_CLASS::_order_bndl&
SOB_CLASS::_order_bndl::operator=(const _order_bndl& bndl)
{
    if( *this != bndl ){
        id = bndl.id;
        sz = bndl.sz;
        exec_cb = bndl.exec_cb;
        cond = bndl.cond;
        trigger =bndl.trigger;
        _copy_union(bndl);
    }
    return *this;
}


SOB_TEMPLATE
typename SOB_CLASS::_order_bndl&
SOB_CLASS::_order_bndl::operator=(_order_bndl&& bndl)
{
    if( *this != bndl ){
        id = bndl.id;
        sz = bndl.sz;
        exec_cb = bndl.exec_cb;
        cond = bndl.cond;
        trigger = bndl.trigger;
        _move_union(bndl);
    }
    return *this;
}


SOB_TEMPLATE
void
SOB_CLASS::_order_bndl::_copy_union(const _order_bndl& bndl)
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
                         ? new OrderParamaters(*bndl.contingent_order)
                         : nullptr;
        break;
    case order_condition::trailing_bracket: /* no break */
    case order_condition::bracket:
        bracket_orders = bndl.bracket_orders
                      ? new bracket_type(*bndl.bracket_orders)
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
    case order_condition::none:
        // TODO what do we want to assert here ?
        break;
    default:
        throw new std::runtime_error("invalid order condition");
    }
}


SOB_TEMPLATE
void
SOB_CLASS::_order_bndl::_move_union(_order_bndl& bndl)
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
    case order_condition::trailing_bracket: /* no break */
    case order_condition::bracket:
        bracket_orders = bndl.bracket_orders;
        bndl.bracket_orders = nullptr;
        break;
    case order_condition::_trailing_stop_active:
        nticks = bndl.nticks;
        break;
    case order_condition::_trailing_bracket_active:
        linked_trailer = bndl.linked_trailer;
        bndl.linked_trailer = nullptr;
        break;
    case order_condition::none:
        // TODO what do we want to assert here ?
        break;
    default:
        throw new std::runtime_error("invalid order condition");
    };
}


SOB_TEMPLATE
SOB_CLASS::_order_bndl::~_order_bndl()
   {
       switch(cond){
       case order_condition::_bracket_active: /* no break */
       case order_condition::one_cancels_other:
           if( linked_order ){
               delete linked_order;
           }
           break;
       case order_condition::trailing_stop: /* no break */
       case order_condition::one_triggers_other:
           if( contingent_order ){
               delete contingent_order;
           }
           break;
       case order_condition::trailing_bracket: /* no break */
       case order_condition::bracket:
           if( bracket_orders ){
               delete bracket_orders;
           }
           break;
       case order_condition::_trailing_bracket_active:
           if( linked_trailer ){
               delete linked_trailer;
           }
           break;
       case order_condition::_trailing_stop_active: /* no break */
       case order_condition::none:
           // TODO what do we want to assert here ?
           break;
       default:
           throw new std::runtime_error("invalid order condition");
       }
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl()
    :
        _order_bndl(),
        is_buy(),
        limit()
    {
    }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl( bool is_buy,
                                 double limit,
                                 id_type id,
                                 size_t sz,
                                 order_exec_cb_type exec_cb,
                                 order_condition cond,
                                 condition_trigger trigger )
   :
       _order_bndl(id, sz, exec_cb, cond, trigger),
       is_buy(is_buy),
       limit(limit)
   {
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl(const stop_bndl& bndl)
   :
       _order_bndl(bndl),
       is_buy(bndl.is_buy),
       limit(bndl.limit)
   {
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl(stop_bndl&& bndl)
   :
        _order_bndl(std::move(bndl)),
        is_buy(bndl.is_buy),
        limit(bndl.limit)
   {
   }

SOB_TEMPLATE
typename SOB_CLASS::stop_bndl&
SOB_CLASS::stop_bndl::operator=(const stop_bndl& bndl)
{
    if( *this != bndl ){
        _order_bndl::operator=(bndl);
        is_buy = bndl.is_buy;
        limit = bndl.limit;
    }
    return *this;
}

SOB_TEMPLATE
typename SOB_CLASS::stop_bndl&
SOB_CLASS::stop_bndl::operator=(stop_bndl&& bndl)
{
    if( *this != bndl ){
        _order_bndl::operator=(std::move(bndl));
        is_buy = bndl.is_buy;
        limit = bndl.limit;
    }
    return *this;
}

};

#undef SOB_TEMPLATE
#undef SOB_CLASS



