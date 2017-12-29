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

#include "simpleorderbook.hpp"

/* 
    The bulk of simpleorderbook is implemented via template code in:

        simpleorderbook.hpp,
        simpleorderbook.tpp

    Interfaces are declared in:

        interfaces.hpp

    Common types/typedefs can be found in:

        types.hpp  
 */

namespace sob{

std::unordered_map<unsigned long long, SimpleOrderbook::ResourceInfo>
SimpleOrderbook::resources;

std::string 
order_type_str(const order_type& ot) /* types.hpp */
{ 
    switch(ot){
    case order_type::market: 
        return "market";
        /* no break */
    case order_type::limit: 
        return "limit";
        /* no break */
    case order_type::stop: 
        return "stop";
        /* no break */
    case order_type::stop_limit: 
        return "stop_limit";
        /* no break */
    default: 
        return "null";
    }
}

std::ostream& 
operator<<(std::ostream& out, const order_info_type& o) /* types.hpp */
{  
    std::string p = (std::get<3>(o) > 0)
                  ? std::string("[") + std::to_string(std::get<3>(o)) + std::string("]")
                  : "";

    std::cout<< order_type_str(std::get<0>(o)) 
             << ' ' << std::string(std::get<1>(o) ? "BUY" : "SELL")
             << ' ' << std::to_string(std::get<4>(o)) 
             << " @ " << std::to_string(std::get<2>(o)) 
             << ' ' << p;

    return out;
}


std::string 
QueryInterface::timestamp_to_str(const time_stamp_type& tp) /* interfaces.hpp */
{ 
    auto sys_tp = std::chrono::system_clock::now() + (tp - clock_type::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys_tp);
    std::string ts = std::ctime(&t);
    ts.resize(ts.size() -1);
    return ts;
}


}; /* sob */




