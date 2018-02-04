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
 *  The bulk of simpleorderbook is implemented via template code in:
 *
 *      simpleorderbook.hpp,
 *      simpleorderbook.tpp
 *
 *  Interfaces are declared in: 
 *
 *      interfaces.hpp
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

std::string 
to_string(const order_type& ot)
{ 
    switch(ot){
    case order_type::market: return "market";
    case order_type::limit: return "limit";
    case order_type::stop: return "stop";
    case order_type::stop_limit: return "stop-limit";
    default: 
        throw std::logic_error( "bad order_type: " +
                std::to_string(static_cast<int>(ot)) );
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
    case callback_msg::kill: return "kill";
    default:
        throw std::logic_error( "bad callback_msg: " +
                std::to_string(static_cast<int>(cm)) );
    }
}

std::string
to_string(const side_of_market& s)
{
    switch(s){
    case side_of_market::bid: return "bid";
    case side_of_market::ask: return "ask";
    case side_of_market::both: return "both";
    default:
        throw std::logic_error( "bad side_of_market: " +
                std::to_string(static_cast<int>(s)) );
    }
}

std::string
to_string(const side_of_trade& s)
{
    switch(s){
    case side_of_trade::buy: return "buy";
    case side_of_trade::sell: return "sell";
    case side_of_trade::all: return "all";
    default:
        throw std::logic_error( "bad side_of_trade: " +
                std::to_string(static_cast<int>(s)) );
    }
}

std::string
to_string(const order_condition& oc)
{
    switch(oc){
    case order_condition::one_cancels_other: return "one-cancels-other";
    case order_condition::one_triggers_other: return "one-triggers-other";
    case order_condition::fill_or_kill: return "fill-or-kill";
    case order_condition::none: return "none";
    default:
        throw std::logic_error( "bad order_condition: " +
                std::to_string(static_cast<int>(oc)) );
    }
}

std::string
to_string(const condition_trigger& ct)
{
    switch(ct){
    case condition_trigger::fill_partial: return "fill-partial";
    case condition_trigger::fill_full: return "fill-full";
    case condition_trigger::none: return "none";
    default:
        throw std::logic_error( "bad condition_trigger: " +
                std::to_string(static_cast<int>(ct)) );
    }
}


std::string
to_string(const clock_type::time_point& tp)
{
    auto sys_tp = std::chrono::system_clock::now() + (tp - clock_type::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys_tp);
    std::string ts = std::ctime(&t);
    ts.resize(ts.size() -1);
    return ts;
}

std::string
to_string(const order_info& oi)
{
    std::stringstream ss;
    ss << OrderParamaters(oi.is_buy, oi.size, oi.limit, oi.stop);
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
       << op.size()
       << (op.limit() ? (" [limit: " + std::to_string(op.limit()) + "]") : "")
       << (op.stop() ? (" [stop: " + std::to_string(op.stop()) + "]") : "");
    return ss.str();
}

std::string
to_string(const AdvancedOrderTicket& aot){
    std::stringstream ss;
    ss << to_string(aot.condition()) << " " << to_string(aot.trigger());
    const OrderParamaters& o1 = aot.order1();
    const OrderParamaters& o2 = aot.order2();
    if( o1 ){
        ss << " " << o1;
    }
    if( o2 ){
        ss << " " << o2;
    }
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

}; /* sob */




