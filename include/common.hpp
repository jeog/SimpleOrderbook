/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_SOB_COMMON
#define JO_SOB_COMMON

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

namespace sob{

/* container class forward decl */
class SimpleOrderbook;

const size_t SOB_MAX_MEMORY = 1024 * 1024 * 128;

typedef unsigned long id_type;

typedef std::ratio<1,4> quarter_tick;
typedef std::ratio<1,10> tenth_tick;
typedef std::ratio<1,32> thirty_secondth_tick;
typedef std::ratio<1,100> hundredth_tick;
typedef std::ratio<1,1000> thousandth_tick;
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

std::string to_string(const order_type& ot);
std::string to_string(const callback_msg& cm);
std::string to_string(const side_of_market& s);
std::string to_string(const clock_type::time_point& tp);
std::string to_string(const order_info_type& oi);

std::ostream& operator<<(std::ostream& out, const order_type& ot);
std::ostream& operator<<(std::ostream& out, const callback_msg& cm);
std::ostream& operator<<(std::ostream& out, const side_of_market& s);
std::ostream& operator<<(std::ostream& out, const clock_type::time_point& tp);
std::ostream& operator<<(std::ostream& out, const order_info_type& oi);

class liquidity_exception
        : public std::logic_error{
public:
    const size_t initial_size;
    const size_t remaining_size;
    const id_type order_id;
    liquidity_exception(size_t initial_size,
                        size_t remaining_size,
                        id_type order_id,
                        std::string msg);
};

}; /* sob */


template<typename IncrementRatio = std::ratio<1,1>,
         double(*RoundFunction)(double) = round,
         unsigned long RoundPrecision = 5>
class TrimmedRational{
    /* only accept ratio between 1/1 and 1/1000000, inclusive */
    static_assert(!std::ratio_greater<IncrementRatio,std::ratio<1,1>>::value,
                  "Increment Ratio > ratio<1,1> ");
    static_assert(!std::ratio_less<IncrementRatio,std::ratio<1,1000000>>::value,
                  "Increment Ratio < ratio<1,1000000> ");

    /* don't let precision overflow 'radj' (9 == floor(log10(pow(2,31)))*/
    static_assert(RoundPrecision <= 9, "RoundPrecision > 9");
    static_assert(RoundPrecision >= 0, "RoundPrecision < 0");
    static constexpr long radj = pow(10,RoundPrecision);

    long _n_whole;
    long _n_incrs;

public:
    typedef TrimmedRational<IncrementRatio,RoundFunction,RoundPrecision> my_type;
    typedef IncrementRatio increment_ratio;
    typedef double(*round_function)(double);

    static constexpr unsigned long rounding_precision = RoundPrecision;

    static constexpr double increment_size =
        static_cast<double>(increment_ratio::num) / increment_ratio::den;

    static constexpr long increment_per_unit =
        static_cast<long>(increment_ratio::den) / increment_ratio::num;

    static constexpr unsigned long long increments_per_unit_ULL =
        static_cast<unsigned long long>(increment_ratio::den) / increment_ratio::num;

    TrimmedRational(long whole, long increments)
        {
           ldiv_t dt = ldiv(increments, increment_per_unit);
           _n_whole = whole + dt.quot;
           _n_incrs = dt.rem;
           if( _n_incrs < 0 ){
               --_n_whole;
               _n_incrs += increment_per_unit;
           }
        }

    explicit TrimmedRational(long increments)
        : TrimmedRational(0, increments)
        {}

    explicit TrimmedRational(double r)
        :
            _n_whole( static_cast<long>(r) - (r < 0 ? 1 : 0) ),
            _n_incrs( RoundFunction((r - _n_whole) * increment_per_unit) )
        {
        }

    inline unsigned long long
    as_increments() const
    { return _n_whole * increments_per_unit_ULL + _n_incrs; }

    inline long
    as_whole() const
    { return _n_whole; }

    inline operator
    double() const
    { return RoundFunction((_n_whole + _n_incrs * increment_size) * radj) / radj; }

    inline my_type
    operator+(const my_type& r) const
    { return my_type(_n_whole + r._n_whole, _n_incrs + r._n_incrs); }

    inline my_type
    operator-(const my_type& r) const
    { return my_type(_n_whole - r._n_whole, _n_incrs - r._n_incrs); }

    inline my_type
    operator+(long i) const
    { return my_type(_n_whole, _n_incrs + i); }

    inline my_type
    operator-(long i) const
    { return my_type(_n_whole, _n_incrs - i); }

    inline my_type
    operator++() // TODO, create a utility function for setting whole/incr
    {
        my_type obj = my_type(_n_whole, _n_incrs + 1);
        this->_n_whole = obj._n_whole;
        this->_n_incrs = obj._n_incrs;
        return *this;
    }

    inline my_type
    operator--()
    {
        my_type obj = my_type(_n_whole, _n_incrs - 1);
        this->_n_whole = obj._n_whole;
        this->_n_incrs = obj._n_incrs;
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
    operator==(const my_type& r) const
    { return (this->_n_whole == r._n_whole) && (this->_n_incrs == r._n_incrs); }

    inline bool
    operator!=(const my_type& r) const
    { return (*this != r); }

    inline bool
    operator>(const my_type& r) const
    {
        if( this->_n_whole > r._n_whole ){
            return true;
        }
        if( this->_n_whole == r._n_whole ){
            return (this->_n_incrs > r._n_incrs);
        }
        return false;
    }

    inline bool
    operator<=(const my_type& r) const
    { return !(*this > r); }

    inline bool
    operator>=(const my_type& r) const
    { return (*this > r) || (*this == r); }

    inline bool
    operator<(const my_type& r) const
    { return !(*this >= r); }

};

#endif 


