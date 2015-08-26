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

template< typename IncrementRatio=std::ratio<1,1>,
          double(*AdjustFunc)(double)=round,
          unsigned long RoundingDigits=5 >
class TrimmedRational{
  static_assert(!std::ratio_greater<IncrementRatio,std::ratio<1,1>>::value,
                "Increment Ratio > ratio<1,1> " );
  static constexpr long _round_adj = pow(10,RoundingDigits);

  div_t _tmp_dt;

public:
  typedef IncrementRatio increment_ratio;

  static constexpr double increment_size =
    (double)increment_ratio::num / increment_ratio::den;
  static constexpr double increments_per_unit =
      (double)increment_ratio::den / increment_ratio::num;

  const long long whole;
  const long long incr;

  TrimmedRational(long long whole, long long incr)
    :
      _tmp_dt(div(incr,increments_per_unit)),
      whole(whole + _tmp_dt.quot),
      incr(_tmp_dt.rem)
    {
    }
  TrimmedRational(double r)
    :
      whole( (long long)r ),
      incr(AdjustFunc((r-whole)*increments_per_unit))
    {
    }
  operator double() const
  {
    return AdjustFunc(((double)(*this)) * _round_adj) / _round_adj;
  }
};

#endif
