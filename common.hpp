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

#define SOB_MAX_MEM (1024 * 1024 * 128)

namespace sob{

/* fwrd decl */
class SimpleOrderbook;

typedef unsigned long       id_type;

typedef std::ratio<1,4>     quarter_tick;
typedef std::ratio<1,10>    tenth_tick;
typedef std::ratio<1,32>    thirty_secondth_tick;
typedef std::ratio<1,100>   hundredth_tick;
typedef std::ratio<1,1000>  thousandth_tick;
typedef std::ratio<1,10000> ten_thousandth_tick;

typedef std::chrono::steady_clock clock_type;

enum class callback_msg{
    cancel = 0,
    fill,
    stop_to_limit // <- guaranteed before limit insert / fill callback
};

enum class order_type {
    null = 0,
    market,
    limit,
    stop,
    stop_limit
};

enum class side_of_market {
    bid = 1,
    ask = -1,
    both = 0
};

typedef std::function<void(callback_msg,id_type,double,size_t)>  order_exec_cb_type;
typedef std::function<void(id_type)> order_admin_cb_type;

/* < order type, is buy, limit price, stop price, size > */
typedef std::tuple<order_type,bool,double, double, size_t> order_info_type;

std::string to_string(const callback_msg& cm);
std::string to_string(const order_type& ot);
std::string to_string(const clock_type::time_point& tp);
std::string to_string(const order_info_type& oi);

std::ostream& operator<<(std::ostream& out, const order_info_type& oi);


class liquidity_exception
        : public std::logic_error{
public:
    liquidity_exception(const char* what)
        : std::logic_error(what) {}
};

class invalid_order
        : public std::invalid_argument{
public:
    invalid_order(const char* what)
        : std::invalid_argument(what) {}
};

class invalid_state
        : public std::runtime_error{ /* logic_error? */
public:
    invalid_state(const char* what)
        : std::runtime_error(what) {}
};

class allocation_error
        : public std::runtime_error{ /* not really a bad_alloc */
public:
    allocation_error(const char* what)
        : std::runtime_error(what) {}
};

}; /* sob */


template<typename IncrementRatio = std::ratio<1,1>,
         double(*RoundFunc)(double) = round,
         unsigned long RoundDigits = 5>
class TrimmedRational{
    static_assert(!std::ratio_greater<IncrementRatio,std::ratio<1,1>>::value,
                  "Increment Ratio > ratio<1,1> ");

    static constexpr long round_adj = pow(10,RoundDigits);

    long long _whole;
    long long _incr;

public:
    typedef TrimmedRational<IncrementRatio,RoundFunc,RoundDigits> my_type;
    typedef IncrementRatio incr_ratio;
    typedef double(*round_function)(double);

    static constexpr unsigned long rounding_digits = RoundDigits;
    static constexpr double incr_size = (double)incr_ratio::num / incr_ratio::den;
    static constexpr double incr_per_unit = (double)incr_ratio::den / incr_ratio::num;

    explicit TrimmedRational(long long incr)
        :
           TrimmedRational(0, incr)
        {
        }

    TrimmedRational(long long whole, long long incr)
        {
           div_t dt = div(incr, incr_per_unit);
           _whole = whole + (dt.rem >= 0 ? dt.quot : -dt.quot-1);
           _incr = dt.rem + (dt.rem >= 0 ? 0 : incr_per_unit);
        }

    explicit TrimmedRational(double r)
        :
            _whole( r >= 0 ? (long long)r : (long long)r - 1),
            _incr( RoundFunc((r-_whole) * incr_per_unit) )
        {
        }

    inline long long
    to_incr() const
    {
        return _whole * incr_per_unit + _incr;
    }

    inline operator 
    double() const
    {
        return RoundFunc((_whole + _incr * incr_size) * round_adj) / round_adj;
    }

    inline my_type
    operator+(const my_type& r) const
    {
        return my_type(_whole + r._whole, _incr + r._incr);
    }

    inline my_type
    operator-(const my_type& r) const
    {
        return my_type(_whole - r._whole, _incr - r._incr);
    }

    inline my_type
    operator+(int i) const
    {
        return my_type(_whole, _incr + i);
    }

    inline my_type
    operator-(int i) const
    {
        return my_type(_whole, _incr - i);
    }

    inline my_type
    operator++() // TODO, create a utility function for setting whole/incr
    {
        my_type obj = my_type(_whole, _incr + 1);
        this->_whole = obj._whole;
        this->_incr = obj._incr;
        return *this;
    }

    inline my_type
    operator--()
    {
        my_type obj = my_type(_whole, _incr - 1);
        this->_whole = obj._whole;
        this->_incr = obj._incr;
        return *this;
    }

    inline my_type
    operator++(int) const
    {
        my_type tmp(*this);
        ++(*this);
        return tmp;
    }

    inline my_type
    operator--(int) const
    {
        my_type tmp(*this);
        --(*this);
        return tmp;
    }

    inline bool
    operator==(const my_type& r)
    {
        return (this->_whole == r._whole) && (this->_incr == r._incr);
    }

    inline bool
    operator!=(const my_type& r)
    {
        return (*this != r);
    }

    inline bool
    operator>(const my_type& r)
    {
        if( this->_whole > r._whole ){
            return true;
        }
        return ((this->_whole == r._whole) ? (this->_incr > r._incr) : false);
    }

    inline bool
    operator<=(const my_type& r)
    {
        return !(*this > r);
    }

    inline bool
    operator>=(const my_type& r)
    {
        return (*this > r) || (*this == r);
    }

    inline bool
    operator<(const my_type& r)
    {
        return !(*this >= r);
    }
};

#endif /* JO_0815_TYPES */


