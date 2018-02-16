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

#include "../include/simpleorderbook.hpp"

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

// TODO create a cleaner mechanism for checking aots/conds against insert calls
SOB_TEMPLATE
id_type
SOB_CLASS::insert_limit_order( bool buy,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               const AdvancedOrderTicket& advanced )
{
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }

    std::unique_ptr<OrderParamaters> cparams1;
    std::unique_ptr<OrderParamaters> cparams2;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        limit = _tick_price_or_throw(limit, "invalid limit price");
        if( advanced ){
            order_condition cond = advanced.condition();
            switch(cond){
            case order_condition::trailing_bracket:
                std::tie(cparams1, cparams2) =
                    _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingBracket&>(
                            advanced)
                    );
                break;
            case order_condition::trailing_stop:
                cparams1 = _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingStop&>(
                            advanced)
                    );
                break;
            case order_condition::bracket:
                cparams2 = _build_aot_order(advanced.order2());
                /* no break */
            case order_condition::one_triggers_other: /* no break */
            case order_condition::one_cancels_other:
                cparams1 = _build_aot_order(advanced.order1());
                break;
            default:
                assert( cond != order_condition::none );
            };

            switch(cond){
            case order_condition::bracket:
                _check_limit_order(buy, limit, cparams2, cond);
                /* no break */
            case order_condition::one_cancels_other:
                _check_limit_order(buy, limit, cparams1, cond);
                break;
            default:
                assert( cond != order_condition::none );
            };
        }

        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(order_type::limit, buy, limit, 0, size, exec_cb,
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams1), std::move(cparams2) );
}


SOB_TEMPLATE
id_type
SOB_CLASS::insert_market_order( bool buy,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                const AdvancedOrderTicket& advanced )
{
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }
    std::unique_ptr<OrderParamaters> cparams1;
    std::unique_ptr<OrderParamaters> cparams2;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        if( advanced ){
            switch( advanced.condition() ){
            case order_condition::trailing_bracket:
                std::tie(cparams1, cparams2) =
                    _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingBracket&>(
                            advanced)
                    );
                break;
            case order_condition::trailing_stop:
                cparams1 = _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingStop&>(
                            advanced)
                    );
                break;
            case order_condition::one_cancels_other:
                throw advanced_order_error("OCO invalid for market order");
            case order_condition::fill_or_kill:
                throw advanced_order_error("FOK invalid for market order");
            case order_condition::bracket:
                cparams2 = _build_aot_order(advanced.order2());
                /* no break */
            default:
                cparams1 = _build_aot_order(advanced.order1());
            };
        }
        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(order_type::market, buy, 0, 0, size, exec_cb,
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams1), std::move(cparams2) );
}


SOB_TEMPLATE
id_type
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced )
{
    return insert_stop_order(buy, stop, 0, size, exec_cb, advanced);
}


SOB_TEMPLATE
id_type
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              double limit,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced )
{
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }
    if( advanced.condition() == order_condition::fill_or_kill ){
        throw advanced_order_error("FOK invalid for market order");
    }
    std::unique_ptr<OrderParamaters> cparams1;
    std::unique_ptr<OrderParamaters> cparams2;
    order_type ot = order_type::stop;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        stop = _tick_price_or_throw(stop, "invalid stop price");
        if( limit ){
            limit = _tick_price_or_throw(limit, "invalid limit price");
            ot = order_type::stop_limit;
        }

        if( advanced ){
            switch( advanced.condition() ){
            case order_condition::trailing_bracket:
                std::tie(cparams1, cparams2) =
                    _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingBracket&>(
                            advanced)
                    );
                break;
            case order_condition::trailing_stop:
                cparams1 = _build_aot_order(!buy, size,
                    reinterpret_cast<const AdvancedOrderTicketTrailingStop&>(advanced)
                    );
                break;
            case order_condition::bracket:
                cparams2 = _build_aot_order(advanced.order2());
                /* no break */
            default:
                cparams1 = _build_aot_order(advanced.order1());
                if( cparams1->is_stop_order() && cparams1->stop() == stop ){
                    throw advanced_order_error("stop orders of same price");
                }
            }
        }
        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(ot, buy, limit, stop, size, exec_cb,
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams1), std::move(cparams2) );
}


SOB_TEMPLATE
bool
SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{
    if(id == 0){
        throw std::invalid_argument("invalid order id(0)");
    }
    return _push_order_and_wait(order_type::null, search_limits_first,
                                0, 0, 0, nullptr, order_condition::none,
                                condition_trigger::none, nullptr, nullptr, id);
}

SOB_TEMPLATE
order_info
SOB_CLASS::get_order_info(id_type id, bool search_limits_first) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return search_limits_first
            ? _order::template as_order_info<limit_chain_type, stop_chain_type>(this, id)
            : _order::template as_order_info<stop_chain_type, limit_chain_type>(this, id);
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
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


SOB_TEMPLATE
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


SOB_TEMPLATE
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


SOB_TEMPLATE
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


SOB_TEMPLATE
void
SOB_CLASS::grow_book_above(double new_max)
{
    auto diff = TickPrice<TickRatio>(new_max) - max_price();

    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_max too far from old max to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(_base, incr, false);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::grow_book_below(double new_min)
{
    if( _base == 1 ){ // can't go any lower
        return;
    }

    TickPrice<TickRatio> new_base(new_min);
    if( new_base < 1 ){
        new_base = TickPrice<TickRatio>(1);
    }

    auto diff = _base - new_base;
    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_min too far from old min to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(new_base, incr, true);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::dump_internal_pointers(std::ostream& out) const
{
    auto println = [&](std::string n, plevel p){
        std::string price;
        try{
            price = std::to_string(_itop(p));
        }catch(std::range_error&){
            price = "N/A";
        }
        out<< std::setw(18) << n << " : "
           << std::setw(14) << price << " : "
           << std::hex << p << std::dec << std::endl;
    };

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::ios sstate(nullptr);
    sstate.copyfmt(out);
    out<< "*** CACHED PLEVELS ***" << std::left << std::endl;
    println("_end", _end);
    println("_high_sell_limit", _high_sell_limit);
    println("_high_buy_stop", _high_buy_stop);
    println("_low_buy_stop", _low_buy_stop);
    println("_ask", _ask);
    println("_last", _last);
    println("_bid", _bid);
    println("_high_sell_stop", _high_sell_stop);
    println("_low_sell_stop", _low_sell_stop);
    println("_low_buy_limit", _low_buy_limit);
    println("_beg", _beg);
    out.copyfmt(sstate);
    /* --- CRITICAL SECTION --- */
}

};

#undef SOB_TEMPLATE
#undef SOB_CLASS
