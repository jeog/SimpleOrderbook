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

// TODO helper obj/func to limit some of the redundancy in here

namespace {

using namespace sob;

/*
 * anything that needs orderbook state e.g valid price ranges, has to be
 *  done internally by the dispatcher while _master_mtx is being held
 */

void
check_order_params(size_t size)
{
    if(size == 0)
        throw std::invalid_argument("invalid order size");
}

void
check_order_params(size_t size, id_type id)
{
    check_order_params(size);

    if(id == 0)
        throw std::invalid_argument("invalid order id(0)");
}

template<typename... Args>
void
check_market_order_params(const AdvancedOrderTicket& advanced, Args... args)
{
    check_order_params(args...);

    if( advanced ){
        switch( advanced.condition() ){
        case order_condition::one_cancels_other:
            throw advanced_order_error("OCO invalid for market order");
        case order_condition::fill_or_kill:
            throw advanced_order_error("FOK invalid for market order");
        case order_condition::all_or_none:
            throw advanced_order_error("AON invalid for market order");
        default: break;
       };
    }
}

template<typename... Args>
void
check_stop_order_params(const AdvancedOrderTicket& advanced, Args... args)
{
    check_order_params(args...);

    if( advanced ){
        switch( advanced.condition() ) {
        case order_condition::fill_or_kill:
            throw advanced_order_error("FOK invalid for stop order");
        case order_condition::all_or_none:
            throw advanced_order_error("AON invalid for stop order");
        default: break;
        };
    }
}

} /* namespace */


namespace sob{

id_type
SOB_CLASS::insert_limit_order( bool buy,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               const AdvancedOrderTicket& advanced )
{
    check_order_params(size);

    return _push_external_order_sync(order_type::limit, buy, limit, 0, size,
                                     exec_cb, advanced );
}

std::future<id_type>
SOB_CLASS::insert_limit_order_async( bool buy,
                                     double limit,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     const AdvancedOrderTicket& advanced )
{
    check_order_params(size);

    return _push_external_order_async(order_type::limit, buy, limit, 0, size,
                                     exec_cb, advanced );
}


id_type
SOB_CLASS::insert_market_order( bool buy,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                const AdvancedOrderTicket& advanced )
{
    check_market_order_params(advanced, size);

    return _push_external_order_sync(order_type::market, buy, 0, 0, size,
                                     exec_cb, advanced);
}

std::future<id_type>
SOB_CLASS::insert_market_order_async(bool buy,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     const AdvancedOrderTicket& advanced )
{
    check_market_order_params(advanced, size);

    return _push_external_order_async(order_type::market, buy, 0, 0, size,
                                      exec_cb, advanced);
}


id_type
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              double limit,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced )
{
    check_stop_order_params(advanced, size);

    order_type ot = limit ? order_type::stop_limit : order_type::stop;

    return _push_external_order_sync(ot, buy, limit, stop, size, exec_cb,
                                     advanced);
}

std::future<id_type>
SOB_CLASS::insert_stop_order_async(bool buy,
                         double stop,
                         double limit,
                         size_t size,
                         order_exec_cb_type exec_cb,
                         const AdvancedOrderTicket& advanced )
{
    check_stop_order_params(advanced, size);

    order_type ot = limit ? order_type::stop_limit : order_type::stop;

    return _push_external_order_async(ot, buy, limit, stop, size, exec_cb,
                                      advanced);
}


bool
SOB_CLASS::pull_order(id_type id)
{
    check_order_params(1, id);

    return _push_external_order_sync(order_type::null, false, 0, 0, 0, nullptr,
                                     AdvancedOrderTicket::null, id);
}

std::future<id_type> // 1 = true, 0 = false
SOB_CLASS::pull_order_async(id_type id)
{
    check_order_params(1, id);

    return _push_external_order_async(order_type::null, false, 0, 0, 0, nullptr,
                                     AdvancedOrderTicket::null, id);
}


id_type
SOB_CLASS::replace_with_limit_order( id_type id,
                                     bool buy,
                                     double limit,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     const AdvancedOrderTicket& advanced )
{
    check_order_params(size, id);

    return _push_external_order_sync(order_type::limit, buy, limit, 0, size,
                                     exec_cb, advanced, id);
}

std::future<id_type>
SOB_CLASS::replace_with_limit_order_async(id_type id,
                                          bool buy,
                                          double limit,
                                          size_t size,
                                          order_exec_cb_type exec_cb,
                                          const AdvancedOrderTicket& advanced )
{
    check_order_params(size, id);

    return _push_external_order_async(order_type::limit, buy, limit, 0, size,
                                     exec_cb, advanced, id);
}


id_type
SOB_CLASS::replace_with_market_order( id_type id,
                                      bool buy,
                                      size_t size,
                                      order_exec_cb_type exec_cb,
                                      const AdvancedOrderTicket& advanced )
{
    check_market_order_params(advanced, size, id);

    return _push_external_order_sync(order_type::market, buy, 0, 0, size,
                                     exec_cb, advanced, id );
}

std::future<id_type>
SOB_CLASS::replace_with_market_order_async(id_type id,
                                           bool buy,
                                           size_t size,
                                           order_exec_cb_type exec_cb,
                                           const AdvancedOrderTicket& advanced )
{
    check_market_order_params(advanced, size, id);

    return _push_external_order_async(order_type::market, buy, 0, 0, size,
                                     exec_cb, advanced, id );
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
    check_stop_order_params(advanced, size, id);

    order_type ot = limit ? order_type::stop_limit : order_type::stop;

    return _push_external_order_sync(ot, buy, limit, stop, size, exec_cb,
                                     advanced, id);
}

std::future<id_type>
SOB_CLASS::replace_with_stop_order_async(id_type id,
                                         bool buy,
                                         double stop,
                                         double limit,
                                         size_t size,
                                         order_exec_cb_type exec_cb,
                                         const AdvancedOrderTicket& advanced )
{
    check_stop_order_params(advanced, size, id);

    order_type ot = limit ? order_type::stop_limit : order_type::stop;

    return _push_external_order_async(ot, buy, limit, stop, size, exec_cb,
                                      advanced, id);
}


order_info
SOB_CLASS::get_order_info(id_type id) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return detail::order::as_order_info(this, id);
    /* --- CRITICAL SECTION --- */
}

}; /* sob */

#undef SOB_CLASS
