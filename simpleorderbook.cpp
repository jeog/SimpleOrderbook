/*
Copyright (C) 2015 Jonathon Ogden < jeog.dev@gmail.com >

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
#include "simpleorderbook.hpp"

/* 
 *  The bulk of simpleorderbook is implemented via template code in:
 *
 *      simpleorderbook.hpp,
 *      simpleorderbook.tpp
 *
 *  Interfaces are declared in: interfaces.hpp
 *
 *  Common types/declarations can be found in: common.hpp
 */

namespace sob{

std::unordered_map<unsigned long long, SimpleOrderbook::ResourceInfo>
SimpleOrderbook::resources;

std::string 
to_string(const order_type& ot)
{ 
    switch(ot){
    case order_type::market: return "market";
    case order_type::limit: return "limit";
    case order_type::stop: return "stop";
    case order_type::stop_limit: return "stop-limit";
    default: 
        throw std::logic_error("bad order_type: " + std::to_string((int)ot));
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
        throw std::logic_error("bad callback_msg: " + std::to_string((int)cm));
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
operator<<(std::ostream& out, const order_info_type& oi)
{  
    std::cout<< to_string(oi);
    return out;
}



}; /* sob */




