/*
Copyright (C) 2015 Jonathon Ogden     < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see http://www.gnu.org/licenses.
*/

#include "interfaces.hpp" /* first or fwrd decl issues with QueryInterface */
#include "types.hpp"

/*DEBUG*/
std::ofstream* pfout = new std::ofstream("tmp.out");
/*DEBUG*/

namespace NativeLayer{

std::string order_type_str(const order_type& ot)
{ /* types.hpp */
  switch(ot){
  case order_type::market: return "market"; /* no break */
  case order_type::limit: return "limit"; /* no break */
  case order_type::stop: return "stop"; /* no break */
  case order_type::stop_limit: return "stop_limit"; /* no break */
  default: return "null";
  }
}

std::ostream& operator<<(std::ostream& out, const order_info_type& o)
{ /* types.hpp */
  price_type prc2 = std::get<3>(o);
  std::string size_str = std::to_string(std::get<4>(o));
  std::string prc1_str = std::to_string(std::get<2>(o));
  std::string side_str = std::get<1>(o) ? "BUY" : "SELL";
  std::string prc2_str =
    prc2 > 0 ? std::string(" [") + std::to_string(prc2) + std::string("]") : "";

  std::cout<< order_type_str(std::get<0>(o)) << ' ' << side_str << ' '
           << size_str << " @ " << prc1_str << prc2_str;
  return out;
}

namespace SimpleOrderbook{

std::string QueryInterface::timestamp_to_str(const time_stamp_type& tp)
{ /* interfaces.hpp */
  std::time_t t = clock_type::to_time_t(tp);
  std::string ts = std::ctime(&t);
  ts.resize(ts.size() -1);
  return ts;
}

};

};
