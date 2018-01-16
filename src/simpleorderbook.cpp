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
to_string(const clock_type::time_point& tp)
{
    auto sys_tp = std::chrono::system_clock::now() + (tp - clock_type::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys_tp);
    std::string ts = std::ctime(&t);
    ts.resize(ts.size() -1);
    return ts;
}

std::string
to_string(const order_info_type& oi)
{
    bool is_buy = std::get<1>(oi);
    double limit = std::get<2>(oi);
    double stop = std::get<3>(oi);
    std::stringstream ss;
    ss << to_string(std::get<0>(oi)) << " "
       << (is_buy ? "Buy" : "Sell") << " "
       << std::get<4>(oi) << " "
       << (limit ? ("[Limit: " + std::to_string(limit) + "]") : "") << " "
       << (stop ? ("[Stop: " + std::to_string(stop) + "]") : "");
    return ss.str();
}

std::ostream&
operator<<(std::ostream& out, const order_type& ot)
{
    out << to_string(ot);
    return out;
}

std::ostream&
operator<<(std::ostream& out, const callback_msg& cm)
{
    out << to_string(cm);
    return out;
}

std::ostream&
operator<<(std::ostream& out, const side_of_market& s)
{
    out << to_string(s);
    return out;
}

std::ostream&
operator<<(std::ostream& out, const clock_type::time_point& tp)
{
    out << to_string(tp);
    return out;
}
std::ostream&
operator<<(std::ostream& out, const order_info_type& oi)
{
    out << to_string(oi);
    return out;
}





}; /* sob */




