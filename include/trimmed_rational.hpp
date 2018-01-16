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

#ifndef JO_SOB_TRIMMED_RATIONAL
#define JO_SOB_TRIMMED_RATIONAL

#include <ratio>
#include <cmath>
#include <limits>

template< typename IncrementRatio,
          double(*RoundFunction)(double) = round,
          unsigned long RoundPrecision = 5 >
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
    typedef IncrementRatio increment_ratio;

    static constexpr double(*round_function)(double) = RoundFunction;
    static constexpr unsigned long round_precision = RoundPrecision;

    static constexpr double increment_size =
        static_cast<double>(increment_ratio::num) / increment_ratio::den;

    static constexpr unsigned long long increments_per_unit =
        static_cast<unsigned long long>(increment_ratio::den) / increment_ratio::num;
    /*
     * use unsigned long long to avoid the cast in as_increments(),
     * assert to avoid overflow in case we remove the lower-bound ratio assert
     */
    static_assert(increments_per_unit <= std::numeric_limits<long>::max(),
                  "increments_per_unit > LONG_MAX");

    TrimmedRational(long whole, long increments)
        {
           ldiv_t dt = ldiv(increments, increments_per_unit);
           _n_whole = whole + dt.quot;
           _n_incrs = dt.rem;
           if( _n_incrs < 0 ){
               --_n_whole;
               _n_incrs += increments_per_unit;
           }
        }

    explicit TrimmedRational(long increments)
        : TrimmedRational(0, increments)
        {}

    explicit TrimmedRational(double r)
        :
            _n_whole( static_cast<long>(r) - (r < 0 ? 1 : 0) ),
            _n_incrs( RoundFunction((r - _n_whole) * increments_per_unit) )
        {
        }

    inline unsigned long long
    as_increments() const
    { return  increments_per_unit * _n_whole + _n_incrs; }

    inline long
    as_whole() const
    { return _n_whole; }

    inline operator
    double() const
    { return RoundFunction((_n_whole + _n_incrs * increment_size) * radj) / radj; }

    inline TrimmedRational
    operator+(const TrimmedRational& r) const
    { return TrimmedRational(_n_whole + r._n_whole, _n_incrs + r._n_incrs); }

    inline TrimmedRational
    operator-(const TrimmedRational& r) const
    { return TrimmedRational(_n_whole - r._n_whole, _n_incrs - r._n_incrs); }

    inline TrimmedRational
    operator+(long i) const
    { return TrimmedRational(_n_whole, _n_incrs + i); }

    inline TrimmedRational
    operator-(long i) const
    { return TrimmedRational(_n_whole, _n_incrs - i); }

    inline TrimmedRational
    operator++() // TODO, create a utility function for setting whole/incr
    {
        TrimmedRational obj = TrimmedRational(_n_whole, _n_incrs + 1);
        this->_n_whole = obj._n_whole;
        this->_n_incrs = obj._n_incrs;
        return *this;
    }

    inline TrimmedRational
    operator--()
    {
        TrimmedRational obj = TrimmedRational(_n_whole, _n_incrs - 1);
        this->_n_whole = obj._n_whole;
        this->_n_incrs = obj._n_incrs;
        return *this;
    }

    inline TrimmedRational
    operator++(int) const
    {
        TrimmedRational tmp(*this);
        ++(*this);
        return tmp;
    }

    inline TrimmedRational
    operator--(int) const
    {
        TrimmedRational tmp(*this);
        --(*this);
        return tmp;
    }

    inline bool
    operator==(const TrimmedRational& r) const
    { return (this->_n_whole == r._n_whole) && (this->_n_incrs == r._n_incrs); }

    inline bool
    operator!=(const TrimmedRational& r) const
    { return !(*this == r); }

    inline bool
    operator>(const TrimmedRational& r) const
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
    operator<=(const TrimmedRational& r) const
    { return !(*this > r); }

    inline bool
    operator>=(const TrimmedRational& r) const
    { return (*this > r) || (*this == r); }

    inline bool
    operator<(const TrimmedRational& r) const
    { return !(*this >= r); }

};

#endif


