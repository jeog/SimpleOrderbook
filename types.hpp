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

#ifndef JO_0815_TYPES
#define JO_0815_TYPES

#include <stdexcept>
#include <exception>
#include <chrono>
#include <ctime>
#include <iostream>
#include <functional>
#include <ratio>
#include <memory>
#include <vector>
#include <map>
#include "trimmed_rational.hpp"

#define SOB_MAX_MEM (1024 * 1024 * 1024)

namespace NativeLayer{

typedef float               price_type;
typedef double              price_diff_type;
typedef unsigned long       size_type, id_type;
typedef long long           size_diff_type;
typedef unsigned long long  large_size_type;

typedef std::ratio<1,100>   default_tick;

namespace SimpleOrderbook{
class QueryInterface;
class LimitInterface;
class FullInterface;
template<typename TickRatio = default_tick, size_type MaxMemory = SOB_MAX_MEM>
class SimpleOrderbook;
/*
 * make sure to add new types as friends to MarketMaker
 */
typedef SimpleOrderbook<std::ratio<1,4>>     QuarterTick;
typedef SimpleOrderbook<std::ratio<1,10>>    TenthTick;
typedef SimpleOrderbook<std::ratio<1,32>>    ThirtySecondthTick;
typedef SimpleOrderbook<>                    HundredthTick, PennyTick, Default;
typedef SimpleOrderbook<std::ratio<1,1000>>  ThousandthTick;
typedef SimpleOrderbook<std::ratio<1,10000>> TenThousandthTick;
}

class MarketMaker;
typedef std::unique_ptr<MarketMaker> pMarketMaker;
typedef std::vector<pMarketMaker> market_makers_type;

typedef std::pair<price_type,size_type>         limit_order_type;
typedef std::pair<price_type,limit_order_type>  stop_order_type;

typedef typename std::chrono::steady_clock      clock_type;

enum class callback_msg{
  cancel = 0,
  fill,
  stop_to_limit // <- guaranteed before limit insert / fill callback
};

enum class order_type {
  market = 0,
  limit,
  stop,
  stop_limit
};

enum class side_of_market {
  bid = 1,
  ask = -1,
  both = 0
};

typedef std::function<void(callback_msg,id_type,
                           price_type,size_type)> order_exec_cb_type;

typedef std::function<void(id_type)> order_admin_cb_type;


/*
template< typename T, typename Pt1, typename Pt2 >
class TypeTag
    : public std::pair<Pt1,Pt2>
{
public:
  typedef T tag_type;
  TypeTag(Pt1 arg1, Pt2 arg2)
    :
      std::pair<Pt1,Pt2>(arg1,arg2)
    {
    }
};

template<typename T>
constexpr TypeTag<T,int,std::string> make_ttp(int i, std::string str)
{
  return TypeTag<T,int,std::string>(i,str);
}*/

inline std::ostream& operator<<(std::ostream& out, limit_order_type lim)
{
  std::cout<< lim.second << ',' << lim.first;
  return out;
}

inline std::ostream& operator<<(std::ostream& out, stop_order_type stp)
{
  std::cout<< stp.first << ',' << stp.second;  // chain to limit overload
  return out;
}

class liquidity_exception
    : public std::logic_error{
public:
  liquidity_exception(const char* what) : std::logic_error(what) { }
};

class invalid_order
    : public std::invalid_argument{
public:
  invalid_order(const char* what) : std::invalid_argument(what) { }
};

class invalid_parameters
    : public std::invalid_argument{
public:
  invalid_parameters(const char* what) : std::invalid_argument(what) { }
};

class cache_value_error
    : public std::runtime_error{
public:
  cache_value_error(const char* what) : std::runtime_error(what) { }
};

class invalid_state
    : public std::runtime_error{ /* logic_error? */
public:
  invalid_state(const char* what) : std::runtime_error(what) { }
};

class callback_overflow
    : public std::runtime_error{
public:
  callback_overflow(const char* what) : std::runtime_error(what) { }
};

class move_error
    : public std::runtime_error{
public:
  move_error(const char* what) : std::runtime_error(what) { }
};

class allocation_error
    : public std::runtime_error{ /* not really a bad_alloc */
public:
  allocation_error(const char* what) : std::runtime_error(what) { }
};

class not_implemented
    : public std::logic_error{
public:
  not_implemented(const char* what) : std::logic_error(what) { }
};

template<typename T1>
inline std::string cat(T1 arg1){ return std::string(arg1); }
template<typename T1, typename... Ts>
inline std::string cat(T1 arg1, Ts... args)
{
  return std::string(arg1) + cat(args...);
}

};
#endif
