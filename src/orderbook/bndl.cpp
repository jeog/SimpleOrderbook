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
        _order_bndl(0, 0, nullptr)
     {
     }


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



SOB_CLASS::_order_bndl::_order_bndl(const _order_bndl& bndl)
    :
        id(bndl.id),
        sz(bndl.sz),
        exec_cb(bndl.exec_cb),
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
        exec_cb(bndl.exec_cb),
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
       case order_condition::none:
           break;
       default:
           throw std::runtime_error("invalid order condition");
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
                                 order_exec_cb_type exec_cb,
                                 order_condition cond,
                                 condition_trigger trigger )
   :
       _order_bndl(id, sz, exec_cb, cond, trigger),
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

};

#undef SOB_CLASS



