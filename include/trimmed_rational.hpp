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

// TODO consider non-member friend opeartor overloads

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
            _n_whole( static_cast<long>(r) - static_cast<long>(r < 0) ),
            _n_incrs( RoundFunction((r - _n_whole) * increments_per_unit) )
        {
            if( _n_incrs == increments_per_unit ){
                ++_n_whole;
                _n_incrs = 0;
            }
        }

    /* conversion methods */
    inline long long
    as_increments() const
    { return  increments_per_unit * _n_whole + _n_incrs; }

    inline long
    as_whole() const
    { return _n_whole; }

    inline operator
    double() const
    { return RoundFunction((_n_whole + _n_incrs * increment_size) * radj) / radj; }


    /* + - += -i for TrimmedRational objects */
    inline TrimmedRational
    operator+(const TrimmedRational& tr) const
    { return TrimmedRational(_n_whole + tr._n_whole, _n_incrs + tr._n_incrs); }

    inline TrimmedRational
    operator-(const TrimmedRational& tr) const
    { return TrimmedRational(_n_whole - tr._n_whole, _n_incrs - tr._n_incrs); }

    inline TrimmedRational
    operator+=(const TrimmedRational& tr)
    { return (*this = *this + tr); }

    inline TrimmedRational
    operator-=(const TrimmedRational& tr)
    { return (*this = *this - tr); }


    /* + - += -i for reals (double) */
    inline TrimmedRational
    operator+(double r) const
    { return *this + TrimmedRational(r); }

    inline TrimmedRational
    operator-(double r) const
    { return *this - TrimmedRational(r); }

    inline TrimmedRational
    operator+=(double r)
    { return (*this = *this + TrimmedRational(r)); }

    inline TrimmedRational
    operator-=(double r)
    { return (*this = *this - TrimmedRational(r)); }


    /* + - += -i for increments (long) */
    inline TrimmedRational
    operator+(long increments) const
    { return TrimmedRational(_n_whole, _n_incrs + increments); }

    inline TrimmedRational
    operator-(long increments) const
    { return TrimmedRational(_n_whole, _n_incrs - increments); }

    inline TrimmedRational
    operator+=(long increments)
    { return (*this = TrimmedRational(_n_whole, _n_incrs + increments)); }

    inline TrimmedRational
    operator-=(long increments)
    { return (*this = TrimmedRational(_n_whole, _n_incrs - increments)); }


    /* + - += -i ++ -- for increments (int to avoid ambiguity) */
    inline TrimmedRational
    operator+(int increments) const
    { return TrimmedRational(_n_whole, _n_incrs + increments); }

    inline TrimmedRational
    operator-(int increments) const
    { return TrimmedRational(_n_whole, _n_incrs - increments); }

    inline TrimmedRational
    operator+=(int increments)
    { return (*this = TrimmedRational(_n_whole, _n_incrs + increments)); }

    inline TrimmedRational
    operator-=(int increments)
    { return (*this = TrimmedRational(_n_whole, _n_incrs - increments)); }

    inline TrimmedRational
    operator++()
    { return (*this = TrimmedRational(_n_whole, _n_incrs + 1)); }

    inline TrimmedRational
    operator--()
    { return (*this = TrimmedRational(_n_whole, _n_incrs - 1)); }

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


    /* == != < > <= >= for TrimmedRational objects */
    inline bool
    operator==(const TrimmedRational& tr) const
    { return (_n_whole == tr._n_whole) && (_n_incrs == tr._n_incrs); }

    inline bool
    operator!=(const TrimmedRational& tr) const
    { return !(*this == tr); }

    inline bool
    operator>(const TrimmedRational& tr) const
    {
        if( _n_whole > tr._n_whole ){
            return true;
        }
        if( _n_whole == tr._n_whole ){
            return (_n_incrs > tr._n_incrs);
        }
        return false;
    }

    inline bool
    operator<=(const TrimmedRational& tr) const
    { return !(*this > tr); }

    inline bool
    operator>=(const TrimmedRational& tr) const
    { return (*this > tr) || (*this == tr); }

    inline bool
    operator<(const TrimmedRational& tr) const
    { return !(*this >= tr); }


    /* == != < > <= >= for increments (long) */
    inline bool
    operator==(long increments) const
    { return *this == TrimmedRational(increments); }

    inline bool
    operator!=(long increments) const
    { return *this != TrimmedRational(increments); }

    inline bool
    operator>(long increments) const
    { return *this > TrimmedRational(increments); }

    inline bool
    operator<=(long increments) const
    { return *this <= TrimmedRational(increments); }

    inline bool
    operator>=(long increments) const
    { return *this >= TrimmedRational(increments); }

    inline bool
    operator<(long increments) const
    { return *this < TrimmedRational(increments); }


    /* == != < > <= >= for increments (int to avoid ambiguity) */
    inline bool
    operator==(int increments) const
    { return *this == TrimmedRational(static_cast<long>(increments)); }

    inline bool
    operator!=(int increments) const
    { return *this != TrimmedRational(static_cast<long>(increments)); }

    inline bool
    operator>(int increments) const
    { return *this > TrimmedRational(static_cast<long>(increments)); }

    inline bool
    operator<=(int increments) const
    { return *this <= TrimmedRational(static_cast<long>(increments)); }

    inline bool
    operator>=(int increments) const
    { return *this >= TrimmedRational(static_cast<long>(increments)); }

    inline bool
    operator<(int increments) const
    { return *this < TrimmedRational(static_cast<long>(increments)); }


    /* == != < > <= >= for reals (double) */
    inline bool
    operator==(double r) const
    { return *this == TrimmedRational(r); }

    inline bool
    operator!=(double r) const
    { return *this != TrimmedRational(r); }

    inline bool
    operator>(double r) const
    { return *this > TrimmedRational(r); }

    inline bool
    operator<=(double r) const
    { return *this <= TrimmedRational(r); }

    inline bool
    operator>=(double r) const
    { return *this >= TrimmedRational(r); }

    inline bool
    operator<(double r) const
    { return *this < TrimmedRational(r); }
};

#endif


