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

#ifndef JO_0815_RATIONAL
#define JO_0815_RATIONAL

#include <ratio>
#include <cmath>

template< typename IncrementRatio = std::ratio<1,1>,
          double(*RoundFunc)(double) = round,
          unsigned long RoundDigits = 5 >
class TrimmedRational{
  static_assert(!std::ratio_greater<IncrementRatio,std::ratio<1,1>>::value,
                "Increment Ratio > ratio<1,1> " );
  static constexpr long _round_adj = pow(10,RoundDigits);

  div_t _tmp_dt;

public:
  typedef TrimmedRational<IncrementRatio,RoundFunc,RoundDigits> my_type;
  typedef IncrementRatio increment_ratio;
  typedef double(*rounding_function)(double);

  static constexpr unsigned long rounding_digits = RoundDigits;
  static constexpr double increment_size = (double)increment_ratio::num
                                                 / increment_ratio::den;
  static constexpr double increments_per_unit = (double)increment_ratio::den
                                                      / increment_ratio::num;

  const long long whole;
  const long long incr;

  TrimmedRational(long long whole, long long incr)
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
  inline operator double() const
  {
    return RoundFunc((whole + incr * increment_size) * _round_adj) / _round_adj;
  }
  inline my_type operator+(const my_type& r) const
  {
    return my_type(this->whole+r.whole, this->incr + r.incr);
  }
  inline my_type operator-(const my_type& r) const
  {
    return my_type(this->whole - r.whole, this->incr - r.incr);
  }
  inline long long as_incr() const
  {
    return (this->whole * increments_per_unit + this->incr);
  }
  // TODO other arithmetic overloads
};

#endif
