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
#include <cmath>
#include <memory>
#include <vector>
#include <map>

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

/* make sure to add new types as friends to MarketMaker */
typedef SimpleOrderbook<std::ratio<1,4>>     QuarterTick;
typedef SimpleOrderbook<std::ratio<1,10>>    TenthTick;
typedef SimpleOrderbook<std::ratio<1,32>>    ThirtySecondthTick;
typedef SimpleOrderbook<>                    HundredthTick, PennyTick, Default;
typedef SimpleOrderbook<std::ratio<1,1000>>  ThousandthTick;
typedef SimpleOrderbook<std::ratio<1,10000>> TenThousandthTick;

} /* SimpleOrderbook */

class MarketMaker;

typedef std::unique_ptr<MarketMaker> pMarketMaker;
typedef std::vector<pMarketMaker> market_makers_type;

typedef typename std::chrono::steady_clock clock_type;
typedef typename clock_type::time_point time_stamp_type;

enum class callback_msg{
    cancel = 0,
    fill,
    stop_to_limit, // <- guaranteed before limit insert / fill callback
    wake
};

enum class order_type {
    null = 0,
    market,
    limit,
    stop,
    stop_limit
};

std::string order_type_str(const order_type& ot);

enum class side_of_market {
    bid = 1,
    ask = -1,
    both = 0
};

typedef std::function<void(callback_msg,id_type,price_type,size_type)>  order_exec_cb_type;
typedef std::function<void(id_type)> order_admin_cb_type;

typedef std::tuple<order_type,bool,price_type, price_type,size_type> order_info_type;

std::ostream& operator<<(std::ostream& out, const order_info_type& o);


class liquidity_exception
    : public std::logic_error{
public:
    liquidity_exception(const char* what)
        : 
            std::logic_error(what) 
        { 
        }
};


class invalid_order
    : public std::invalid_argument{
public:
    invalid_order(const char* what)
        : 
            std::invalid_argument(what) 
        { 
        }
};


class invalid_parameters
    : public std::invalid_argument{
public:
    invalid_parameters(const char* what)
        : 
            std::invalid_argument(what) 
        { 
        }
};


class cache_value_error
    : public std::runtime_error{
public:
    cache_value_error(const char* what)
        : 
            std::runtime_error(what) 
        { 
        }
};


class invalid_state
    : public std::runtime_error{ /* logic_error? */
public:
    invalid_state(const char* what)
        : 
            std::runtime_error(what) 
        { 
        }
};


class callback_overflow
    : public std::runtime_error{
public:
    callback_overflow(const char* what)
        : 
            std::runtime_error(what) 
        { 
        }
};


class move_error
    : public std::runtime_error{
public:
    move_error(const char* what)
        : 
            std::runtime_error(what) 
        { 
        }
};


class allocation_error
    : public std::runtime_error{ /* not really a bad_alloc */
public:
    allocation_error(const char* what)
        : 
            std::runtime_error(what) 
        { 
        }
};


class not_implemented
    : public std::logic_error{
public:
    not_implemented(const char* what) 
        : 
            std::logic_error(what) 
        { 
        }
};


template<typename T1>
inline std::string 
cat(T1 arg1)
{ 
    return std::string(arg1); 
}

template<typename T1, typename... Ts>
inline std::string 
cat(T1 arg1, Ts... args)
{
    return std::string(arg1) + cat(args...);
}


}; /* NativeLayer */


template<typename IncrementRatio = std::ratio<1,1>,
         double(*RoundFunc)(double) = round,
         unsigned long RoundDigits = 5>
class TrimmedRational{

    static_assert(!std::ratio_greater<IncrementRatio,std::ratio<1,1>>::value,
                  "Increment Ratio > ratio<1,1> ");

    static constexpr long _round_adj = pow(10,RoundDigits);
    div_t _tmp_dt;

public:
    typedef TrimmedRational<IncrementRatio,RoundFunc,RoundDigits> my_type;
    typedef IncrementRatio increment_ratio;
    typedef double(*rounding_function)(double);

    static constexpr unsigned long rounding_digits = RoundDigits;

    static constexpr double increment_size = 
        (double)increment_ratio::num / increment_ratio::den;

    static constexpr double increments_per_unit = 
        (double)increment_ratio::den / increment_ratio::num;

    const long long whole;
    const long long incr;

    TrimmedRational(long long whole, long long incr)
        :
            _tmp_dt(div(incr,increments_per_unit)),
            whole(whole + (_tmp_dt.rem >= 0 ? _tmp_dt.quot : -_tmp_dt.quot-1)),
            incr(_tmp_dt.rem + (_tmp_dt.rem >= 0 ? 0 : increments_per_unit))
        {
        }

    explicit TrimmedRational(long long incr)
        :
            _tmp_dt(div(incr,increments_per_unit)),
            whole(whole + (_tmp_dt.rem >= 0 ? _tmp_dt.quot : -_tmp_dt.quot-1)),
            incr(_tmp_dt.rem + (_tmp_dt.rem >= 0 ? 0 : increments_per_unit))
        {
        }

    TrimmedRational(double r)
        : /* careful, non explicit constr */
            whole( r >= 0 ? (long long)r : (long long)r - 1),
            incr( RoundFunc((r-whole)*increments_per_unit) )
        {
        }

    inline operator 
    double() const
    {
        return RoundFunc((whole + incr * increment_size) * _round_adj) / _round_adj;
    }

    inline my_type 
    operator+(const my_type& r) const
    {
        return my_type(this->whole+r.whole, this->incr + r.incr);
    }

    inline my_type 
    operator-(const my_type& r) const
    {
        return my_type(this->whole - r.whole, this->incr - r.incr);
    }

    inline long long 
    to_incr() const
    {
        return (this->whole * increments_per_unit + this->incr);
    }
    
    // TODO other arithmetic overloads
};

#endif /* JO_0815_TYPES */


