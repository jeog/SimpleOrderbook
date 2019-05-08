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

#include <sstream>
#include "../include/simpleorderbook.hpp"

/* 
 *  The bulk of simpleorderbook is implemented in:
 *
 *      src/orderbook/core.cpp
 *      src/orderbook/advanced.cpp
 *      src/orderbook/orders.cpp
 *      src/orderbook/impl.tpp
 *      src/orderbook/specials.tpp
 *
 *  Interfaces are declared in: 
 *
 *      includeinterfaces.hpp
 */

namespace sob{

SOB_RESOURCE_MANAGER<FullInterface, SimpleOrderbook::ImplDeleter>
SimpleOrderbook::master_rmanager("master");

SimpleOrderbook::ImplDeleter::ImplDeleter( std::string tag,
                                           std::string msg,
                                           std::ostream& out )
    :
        _tag(tag),
        _msg(msg),
        _out(out)
    {
    }

void
SimpleOrderbook::ImplDeleter::operator()(FullInterface *i) const
{
    delete i;
    if( _msg.size() > 0 ){
        _out << "ImplDeleter :: " <<  _tag
             << " :: " << std::hex << i << std::dec
             << " :: " << _msg << std::endl;

    }
}


liquidity_exception::liquidity_exception( size_t initial_size,
                                          size_t remaining_size,
                                          id_type order_id,
                                          std::string msg )
    :
        std::logic_error(
            "order #" + std::to_string(order_id) + " only filled " +
            std::to_string(initial_size - remaining_size) + "/" +
            std::to_string(initial_size) + "; " + msg
            ),
        initial_size(initial_size),
        remaining_size(remaining_size),
        order_id(order_id)
    {
    }


#define THROW_ENUM_TO_STR_EXC(name, val) \
    throw std::runtime_error("bad " + std::string(name) + ": " \
                             + std::to_string(static_cast<int>(val)) )

std::string 
to_string(const order_type& ot)
{ 
    switch(ot){
    case order_type::market: return "market";
    case order_type::limit: return "limit";
    case order_type::stop: return "stop";
    case order_type::stop_limit: return "stop-limit";
    default: THROW_ENUM_TO_STR_EXC("order_type", ot);
    }
}

std::string
to_string(const callback_msg& cm)
{
    switch(cm){
    case callback_msg::cancel: return "cancel";
    case callback_msg::fill: return "fill";
    case callback_msg::stop_to_limit: return "stop-to-limit";
    case callback_msg::stop_to_market: return "stop-to-market";
    case callback_msg::trigger_OCO: return "trigger-OCO";
    case callback_msg::trigger_OTO: return "trigger-OTO";
    case callback_msg::trigger_BRACKET_open: return "trigger-BRACKET-open";
    case callback_msg::trigger_BRACKET_close: return "trigger-BRACKET-close";
    case callback_msg::trigger_trailing_stop: return "trigger-trailing-stop";
    case callback_msg::adjust_trailing_stop: return "adjust-trailing-stop";
    case callback_msg::kill: return "kill";
    default: THROW_ENUM_TO_STR_EXC("callback_msg", cm);
    }
}

std::string
to_string(const side_of_market& s)
{
    switch(s){
    case side_of_market::bid: return "bid";
    case side_of_market::ask: return "ask";
    case side_of_market::both: return "bid/ask";
    default: THROW_ENUM_TO_STR_EXC("side_of_market", s);
    }
}

std::string
to_string(const side_of_trade& s)
{
    switch(s){
    case side_of_trade::buy: return "buy";
    case side_of_trade::sell: return "sell";
    case side_of_trade::both: return "buy/sell";
    default: THROW_ENUM_TO_STR_EXC("side_of_trade", s);
    }
}

std::string
to_string(const order_condition& oc)
{
    switch(oc){
    case order_condition::one_cancels_other: return "one-cancels-other";
    case order_condition::one_triggers_other: return "one-triggers-other";
    case order_condition::fill_or_kill: return "fill-or-kill";
    case order_condition::bracket: return "bracket";
    case order_condition::_bracket_active: return "bracket-active";
    case order_condition::trailing_stop: return "trailing-stop";
    case order_condition::_trailing_stop_active: return "trailing-stop-active";
    case order_condition::trailing_bracket: return "trailing-bracket";
    case order_condition::_trailing_bracket_active: return "trailing-bracket-active";
    case order_condition::all_or_nothing: return "all-or-noting";
    case order_condition::none: return "none";
    default: THROW_ENUM_TO_STR_EXC("order_condition", oc);
    }
}

std::string
to_string(const condition_trigger& ct)
{
    switch(ct){
    case condition_trigger::fill_partial: return "fill-partial";
    case condition_trigger::fill_full: return "fill-full";
    case condition_trigger::none: return "none";
    default: THROW_ENUM_TO_STR_EXC("condition_trigger", ct);
    }
}

#undef THROW_ENUM_TO_STR_EXC


std::string
to_string(const clock_type::time_point& tp)
{
    using namespace std::chrono;

    auto sys_tp = system_clock::now() +
       duration_cast<microseconds>(tp - clock_type::now());

    auto t = system_clock::to_time_t(sys_tp);
    std::string buf(24, '\0');
    strftime( &buf[0], 24, "%Y-%m-%d-%H-%M", localtime(&t) );
    buf.erase( buf.find_first_of('\0') );

    static const long long MSEC_IN_DAY = 24 * 60 * 60 * 1000000LL;
    long long ms = sys_tp.time_since_epoch().count() % MSEC_IN_DAY;
    return buf + "-" + std::to_string(ms);
}

std::string
to_string(const order_info& oi)
{
    std::stringstream ss;
    ss << OrderParamatersByPrice(oi.is_buy, oi.size, oi.limit, oi.stop);
    if( oi.advanced ){
       ss << " " << oi.advanced;
    }
    return ss.str();
}

std::string
to_string(const OrderParamaters& op)
{
    std::stringstream ss;
    ss << (op.is_buy() ? "buy" : "sell") << " "
       << op.get_order_type() << " "
       << op.size();
    if( op.is_by_price() ){
        double l = op.limit_price();
        double s = op.stop_price();
        ss << (l ? (" [limit: " + std::to_string(l) + "]") : "")
           << (s ? (" [stop: " + std::to_string(s) + "]") : "");
    }else{
        size_t l = op.limit_nticks();
        size_t s = op.stop_nticks();
        ss << (l ? (" [limit ticks: " + std::to_string(l) + "]") : "")
           << (s ? (" [stop ticks: " + std::to_string(s) + "]") : "");
    }
    return ss.str();
}

std::string
to_string(const AdvancedOrderTicket& aot){
    std::stringstream ss;
    ss << to_string(aot.condition()) << " " << to_string(aot.trigger());
    const OrderParamaters *o1 = aot.order1();
    const OrderParamaters *o2 = aot.order2();
    if( o1 ){
        ss << " " << *o1;
    }
    if( o2 ){
        ss << " " << *o2;
    }
    return ss.str();
}

std::string
to_string(const timesale_entry_type& entry){
    std::stringstream ss;
    ss << '<' << std::get<0>(entry) << ", " << std::get<1>(entry)
       << ", " << std::get<2>(entry) << '>';
    return ss.str();
}

std::ostream&
operator<<(std::ostream& out, const order_type& ot)
{ return ( out << to_string(ot)); }

std::ostream&
operator<<(std::ostream& out, const callback_msg& cm)
{ return (out << to_string(cm)); }

std::ostream&
operator<<(std::ostream& out, const side_of_market& s)
{ return (out << to_string(s)); }

std::ostream&
operator<<(std::ostream& out, const side_of_trade& s)
{ return (out << to_string(s)); }

std::ostream&
operator<<(std::ostream& out, const clock_type::time_point& tp)
{ return (out << to_string(tp)); }

std::ostream&
operator<<(std::ostream& out, const order_condition& oc)
{ return (out << to_string(oc)); }
std::ostream&
operator<<(std::ostream& out, const condition_trigger& ct)
{ return (out << to_string(ct)); }

std::ostream&
operator<<(std::ostream& out, const order_info& oi)
{ return (out << to_string(oi)); }

std::ostream&
operator<<(std::ostream& out, const OrderParamaters& op)
{ return (out << to_string(op)); }

std::ostream&
operator<<(std::ostream& out, const AdvancedOrderTicket& aot)
{ return (out << to_string(aot)); }

std::ostream&
operator<<(std::ostream& out, const timesale_entry_type& entry)
{ return (out << to_string(entry)); }

order_info::order_info()
    :
        type(sob::order_type::null),
        is_buy(0),
        limit(0),
        stop(0),
        size(0),
        advanced(sob::AdvancedOrderTicket::null)
    {
    }

order_info::order_info( order_type type,
                        bool is_buy,
                        double limit,
                        double stop,
                        size_t size,
                        const AdvancedOrderTicket& advanced )
    :
        type(type),
        is_buy(is_buy),
        limit(limit),
        stop(stop),
        size(size),
        advanced(advanced)
    {
    }

order_info::order_info(const order_info& oi)
    :
        type(oi.type),
        is_buy(oi.is_buy),
        limit(oi.limit),
        stop(oi.stop),
        size(oi.size),
        advanced(oi.advanced)
    {
    }

}; /* sob */




