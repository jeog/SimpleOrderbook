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

#include <iomanip>

#include "../../include/simpleorderbook.hpp"
#include "../../include/order_util.hpp"
#include "specials.tpp"

#define SOB_CLASS SimpleOrderbook::SimpleOrderbookBase

// NOTE - only explicitly instantiate members needed for link and not
//        done implicitly. If (later) called from outside public.cpp
//        need to add them.

namespace sob{

id_type
SOB_CLASS::insert_limit_order( bool buy,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               const AdvancedOrderTicket& advanced )
{
    if(size == 0)
        throw std::invalid_argument("invalid order size");

    order_condition cond = advanced.condition();
    condition_trigger trig = advanced.trigger();

    std::unique_ptr<OrderParamaters> pp1, pp2;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        limit = _tick_price_or_throw(limit, "invalid limit price");

        if( advanced ){
            std::tie(pp1, pp2) = _build_advanced_params(buy, size, advanced);
            switch( cond ){
            case order_condition::bracket:
                _check_limit_order(buy, limit, pp2, cond );
                break;
            case order_condition::one_cancels_other:
                _check_limit_order(buy, limit, pp1, cond );
                break;
            default: break;
            };
        }
        /* --- CRITICAL SECTION --- */
    }

    return _push_order_and_wait(order_type::limit, buy, limit, 0, size, exec_cb,
                                cond, trig, std::move(pp1), std::move(pp2) );
}


id_type
SOB_CLASS::insert_market_order( bool buy,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                const AdvancedOrderTicket& advanced )
{
    if(size == 0)
        throw std::invalid_argument("invalid order size");

    order_condition cond = advanced.condition();
    condition_trigger trig = advanced.trigger();

    std::unique_ptr<OrderParamaters> pp1, pp2;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        if( advanced ){
            switch( cond ){
            case order_condition::one_cancels_other:
                throw advanced_order_error("OCO invalid for market order");
            case order_condition::fill_or_kill:
                throw advanced_order_error("FOK invalid for market order");
            case order_condition::all_or_nothing:
                throw advanced_order_error("AON invalid for market order");
            default: break;
            };
            std::tie(pp1, pp2) = _build_advanced_params(buy, size, advanced);
        }
        /* --- CRITICAL SECTION --- */
    }

    return _push_order_and_wait(order_type::market, buy, 0, 0, size, exec_cb,
                                cond, trig, std::move(pp1), std::move(pp2) );
}


id_type
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced )
{
    return insert_stop_order(buy, stop, 0, size, exec_cb, advanced);
}



id_type
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              double limit,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced )
{
    if(size == 0)
        throw std::invalid_argument("invalid order size");

    order_type ot = order_type::stop;
    order_condition cond = advanced.condition();
    condition_trigger trig = advanced.trigger();

    std::unique_ptr<OrderParamaters> pp1, pp2;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        stop = _tick_price_or_throw(stop, "invalid stop price");

        if( limit ){
            limit = _tick_price_or_throw(limit, "invalid limit price");
            ot = order_type::stop_limit;
        }

        if( advanced ){
            switch( cond) {
            case order_condition::fill_or_kill:
                throw advanced_order_error("FOK invalid for stop order");
            case order_condition::all_or_nothing:
                throw advanced_order_error("AON invalid for stop order");
            default: break;
            };

            std::tie(pp1, pp2) = _build_advanced_params(buy, size, advanced);
            if( pp1->stop_price() == stop )
                throw advanced_order_error("stop orders of same price");

        }
        /* --- CRITICAL SECTION --- */
    }

    return _push_order_and_wait(ot, buy, limit, stop, size, exec_cb, cond,
                                trig, std::move(pp1), std::move(pp2) );
}


bool
SOB_CLASS::pull_order(id_type id)
{
    if(id == 0)
        throw std::invalid_argument("invalid order id(0)");

    return _push_order_and_wait(order_type::null, false, 0, 0, 0, nullptr,
                                order_condition::none, condition_trigger::none,
                                nullptr, nullptr, id);
}


order_info
SOB_CLASS::get_order_info(id_type id) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return detail::order::as_order_info(this, id);
    /* --- CRITICAL SECTION --- */
}


id_type
SOB_CLASS::replace_with_limit_order( id_type id,
                                     bool buy,
                                     double limit,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     const AdvancedOrderTicket& advanced )
{
    id_type id_new = 0;
    if( pull_order(id) ){
        id_new = insert_limit_order(buy, limit, size, exec_cb, advanced);
    }
    return id_new;
}


id_type
SOB_CLASS::replace_with_market_order( id_type id,
                                      bool buy,
                                      size_t size,
                                      order_exec_cb_type exec_cb,
                                      const AdvancedOrderTicket& advanced )
{
    id_type id_new = 0;
    if( pull_order(id) ){
        id_new = insert_market_order(buy, size, exec_cb, advanced);
    }
    return id_new;
}


id_type
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    double stop,
                                    size_t size,
                                    order_exec_cb_type exec_cb,
                                    const AdvancedOrderTicket& advanced )
{
    id_type id_new = 0;
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, size, exec_cb, advanced);
    }
    return id_new;
}


id_type
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    double stop,
                                    double limit,
                                    size_t size,
                                    order_exec_cb_type exec_cb,
                                    const AdvancedOrderTicket& advanced )
{
    id_type id_new = 0;
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, limit, size, exec_cb, advanced);
    }
    return id_new;
}



}; /* sob */

#undef SOB_CLASS
