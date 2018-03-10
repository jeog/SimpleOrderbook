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

#ifndef JO_SOB_TICK_PRICE
#define JO_SOB_TICK_PRICE

#include <ratio>
#include <limits>

namespace tp {

#ifdef _MSC_VER
#include "cx_math.h"
    using namespace cx;
#else
#include <cmath>
    using std::pow;
    using std::log10;
    using std::round;
#endif /* _MSC_VER */

template< typename T>
constexpr T const&
max(T const& a, T const& b)
{ return (a > b) ? a : b; }

template<typename TickRatio>
struct round_precision {
    static constexpr int value = max(
        5, static_cast<int>(tp::round(tp::log10(TickRatio::den / TickRatio::num)))
    );
};

}; /* tp */


template< typename TickRatio,
          double(*RoundFunction)(double) = tp::round,
          unsigned long RoundPrecision = tp::round_precision<TickRatio>::value >
class TickPrice{
    /* only accept ratio between 1/1 and 1/1000000, inclusive */
    static_assert(!std::ratio_greater<TickRatio,std::ratio<1,1>>::value,
                  "TickRatio > ratio<1,1> ");
    static_assert(!std::ratio_less<TickRatio,std::ratio<1,1000000>>::value,
                  "TickRatio < ratio<1,1000000> ");

    /* only accept ratios evenly divisible into 1 */
    static_assert( (TickRatio::den % TickRatio::num) == 0,
                   "TickRatio::den not a multiple of TickRatio::num");

    /* don't let precision overflow 'radj' (9 == floor(log10(pow(2,31)))*/
    static_assert(RoundPrecision <= 9, "RoundPrecision > 9");
    static_assert(RoundPrecision >= 0, "RoundPrecision < 0");
    static constexpr long radj = tp::round(tp::pow(10, RoundPrecision));

    long _n_whole;
    long _n_ticks;

public:
    typedef TickRatio tick_ratio;

    static constexpr double(*round_function)(double) = RoundFunction;
    static constexpr unsigned long round_precision = RoundPrecision;

    static constexpr double tick_size =
        static_cast<double>(tick_ratio::num) / tick_ratio::den;

    static constexpr unsigned long long ticks_per_unit =
        static_cast<unsigned long long>(tick_ratio::den) / tick_ratio::num;
    /*
     * use unsigned long long to avoid the cast in as_ticks(),
     * assert to avoid overflow in case we remove the lower-bound ratio assert
     */
    static_assert(ticks_per_unit <= std::numeric_limits<long>::max(),
                  "ticks_per_unit > LONG_MAX");

    /* be sure precision is large enough for tick size */
    static_assert( RoundPrecision >= tp::round(tp::log10(ticks_per_unit)),
                   "RoundPrecision not large enough for this ratio");

    TickPrice(long whole, long ticks)
        {
           ldiv_t dt = ldiv(ticks, ticks_per_unit);
           _n_whole = whole + dt.quot;
           _n_ticks = dt.rem;
           if( _n_ticks < 0 ){
               --_n_whole;
               _n_ticks += ticks_per_unit;
           }
        }

    explicit TickPrice(long ticks)
        : TickPrice(0, ticks)
        {}

    explicit TickPrice(int ticks)
        : TickPrice(0, static_cast<long>(ticks))
        {}

    explicit TickPrice(double r)
        :
            _n_whole( static_cast<long>(r) - static_cast<long>(r < 0) ),
            _n_ticks( static_cast<long>(RoundFunction((r - _n_whole) * ticks_per_unit)) )
        {
            if( _n_ticks == ticks_per_unit ){
                ++_n_whole;
                _n_ticks = 0;
            }
        }

    /* conversion methods */
    inline long long
    as_ticks() const
    { return  ticks_per_unit * _n_whole + _n_ticks; }

    inline long
    as_whole() const
    { return _n_whole; }

    inline operator
    double() const
    { return RoundFunction((_n_whole + _n_ticks * tick_size) * radj) / radj; }

    /* + - */
    inline TickPrice
    operator+(const TickPrice& tr) const
    { return TickPrice(_n_whole + tr._n_whole, _n_ticks + tr._n_ticks); }

    inline TickPrice
    operator-(const TickPrice& tr) const
    { return TickPrice(_n_whole - tr._n_whole, _n_ticks - tr._n_ticks); }

    inline TickPrice
    operator+(long ticks) const
    { return TickPrice(_n_whole, _n_ticks + ticks); }

    inline TickPrice
    operator-(long ticks) const
    { return TickPrice(_n_whole, _n_ticks - ticks); }

    inline TickPrice
    operator+(int ticks) const
    { return TickPrice(_n_whole, _n_ticks + ticks); }

    inline TickPrice
    operator-(int ticks) const
    { return TickPrice(_n_whole, _n_ticks - ticks); }

    inline TickPrice
    operator+(double r) const
    { return *this + TickPrice(r); }

    inline TickPrice
    operator-(double r) const
    { return *this - TickPrice(r); }


    /* += -= */
    inline TickPrice
    operator+=(const TickPrice& tr)
    { return (*this = *this + tr); }

    inline TickPrice
    operator-=(const TickPrice& tr)
    { return (*this = *this - tr); }

    template<typename T>
    inline TickPrice
    operator+=(T val)
    { return (*this = *this + TickPrice(val)); }

    template<typename T>
    inline TickPrice
    operator-=(T val)
    { return (*this = *this - TickPrice(val)); }


    /* ++ -- */
    inline TickPrice
    operator++()
    { return (*this = TickPrice(_n_whole, _n_ticks + 1)); }

    inline TickPrice
    operator--()
    { return (*this = TickPrice(_n_whole, _n_ticks - 1)); }

    inline TickPrice
    operator++(int) const
    {
        TickPrice tmp(*this);
        ++(*this);
        return tmp;
    }

    inline TickPrice
    operator--(int) const
    {
        TickPrice tmp(*this);
        --(*this);
        return tmp;
    }


    /* == != < > <= >= */
    inline bool
    operator==(const TickPrice& tr) const
    { return (_n_whole == tr._n_whole) && (_n_ticks == tr._n_ticks); }

    inline bool
    operator!=(const TickPrice& tr) const
    { return !(*this == tr); }

    inline bool
    operator>(const TickPrice& tr) const
    {
        if( _n_whole > tr._n_whole ){
            return true;
        }
        if( _n_whole == tr._n_whole ){
            return (_n_ticks > tr._n_ticks);
        }
        return false;
    }

    inline bool
    operator<=(const TickPrice& tr) const
    { return !(*this > tr); }

    inline bool
    operator>=(const TickPrice& tr) const
    { return (*this > tr) || (*this == tr); }

    inline bool
    operator<(const TickPrice& tr) const
    { return !(*this >= tr); }

    template<typename T>
    inline bool
    operator==(T ticks) const
    { return *this == TickPrice(ticks); }

    template<typename T>
    inline bool
    operator!=(T ticks) const
    { return *this != TickPrice(ticks); }

    template<typename T>
    inline bool
    operator>(T ticks) const
    { return *this > TickPrice(ticks); }

    template<typename T>
    inline bool
    operator<=(T ticks) const
    { return *this <= TickPrice(ticks); }

    template<typename T>
    inline bool
    operator>=(T ticks) const
    { return *this >= TickPrice(ticks); }

    template<typename T>
    inline bool
    operator<(T ticks) const
    { return *this < TickPrice(ticks); }
};


/* + - (reversed operands) */
template<typename T, typename A>
inline TickPrice<A>
operator+(T val, const TickPrice<A>& tr)
{ return tr + val; }

template<typename T, typename A>
inline TickPrice<A>
operator-(T val, const TickPrice<A>& tr)
{ return TickPrice<A>(val) - tr; }


/* += -= (reversed operands) */
template<typename T, typename A>
inline TickPrice<A>
operator+=(T val, const TickPrice<A>& tr)
{ return (val = val + tr); }

template<typename T, typename A>
inline TickPrice<A>
operator-=(T val, const TickPrice<A>& tr)
{ return (val = val - tr); }


/* == != < > <= >= for other objects (reversed operands) */
template<typename T, typename A>
inline bool
operator==(T ticks, const TickPrice<A>& tr)
{ return tr == ticks; }

template<typename T, typename A>
inline bool
operator!=(T ticks, const TickPrice<A>& tr)
{ return tr != ticks; }

template<typename T, typename A>
inline bool
operator>(T ticks, const TickPrice<A>& tr)
{ return tr <= ticks; }

template<typename T, typename A>
inline bool
operator<=(T ticks, const TickPrice<A>& tr)
{ return tr > ticks; }

template<typename T, typename A>
inline bool
operator>=(T ticks, const TickPrice<A>& tr)
{ return tr < ticks; }

template<typename T, typename A>
inline bool
operator<(T ticks, const TickPrice<A>& tr)
{ return tr >= ticks; }


#endif


