/*
The MIT License(MIT)

Copyright(c) 2015, 2016 Ben Deane

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/*
constexpr math for MSCV compiler

Original code from https://github.com/elbeno/constexpr

Modifications by Jonathon Ogden are covered under the included GPL liscense;
errors are his and his alone.
*/

#pragma once

#include <limits>
#include <type_traits>
#include <stdexcept>

namespace cx {

template <typename A1, typename A2>
struct promoted
{ using type = double; };

template <typename A>
struct promoted<long double, A>
{ using type = long double; };

template <typename A>
struct promoted<A, long double>
{ using type = long double; };

template <>
struct promoted<long double, long double>
{ using type = long double; };

template <typename A1, typename A2>
using promoted_t = typename promoted<A1, A2>::type;

template <typename T>
constexpr T 
abs(T x)
{ return x >= 0 ? x : x < 0 ? -x : throw std::runtime_error("abs"); }

namespace detail {
    // test whether values are within machine epsilon, used for algorithm
    // termination
    template <typename T>
    constexpr bool 
    feq(T x, T y)
    { return abs(x - y) <= std::numeric_limits<T>::epsilon(); }

    //----------------------------------------------------------------------------
    // raise to integer power
    template <typename FloatingPoint>
    constexpr FloatingPoint 
    ipow( FloatingPoint x, 
          int n,
          typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
    {
        return (n == 0) 
            ? FloatingPoint{1} 
            : n == 1 
                ? x 
                : n > 1 
                    ? ((n & 1) ? x * ipow(x, n-1) : ipow(x, n/2) * ipow(x, n/2)) 
                    : FloatingPoint{1} / ipow(x, -n);
    }

    //----------------------------------------------------------------------------
    // exp by Taylor series expansion  
    template <typename T>
    constexpr T 
    exp(T x, T sum, T n, int i, T t)
    {
        return feq(sum, sum + t / n) 
            ? sum 
            : exp(x, sum + t / n, n * i, i + 1, t * x);
    }    
} /* detail */

template <typename FloatingPoint>
constexpr FloatingPoint 
exp( FloatingPoint x,
     typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
{ return detail::exp(x, FloatingPoint{ 1 }, FloatingPoint{ 1 }, 2, x); }

template <typename Integral>
constexpr double 
exp( Integral x,
     typename std::enable_if<std::is_integral<Integral>::value>::type* = 0 )
{ return detail::exp<double>(x, 1.0, 1.0, 2, x); }


//----------------------------------------------------------------------------
// floor and ceil: each works in terms of the other for negative numbers
// The algorithm proceeds by "binary search" on the increment.
// But in order not to overflow the max compile-time recursion depth
// (say 512) we need to perform an n-ary search, where:
// n = 2^(numeric_limits<T>::max_exponent/512 + 1)
// (The +1 gives space for other functions in the stack.)
// For float, a plain binary search is fine, because max_exponent = 128.
// For double, max_exponent = 1024, so we need n = 2^3 = 8.
// For long double, max_exponent = 16384, so we need n = 2^33. Oops. Looks
// like floor/ceil for long double can only exist for C++14 where we are not
// limited to recursion.
namespace detail {

    template <typename T>
    constexpr T 
    floor2(T x, T guess, T inc)
    {
        return guess + inc <= x 
            ? floor2(x, guess + inc, inc) 
            : inc <= T{1} 
                ? guess 
                : floor2(x, guess, inc/T{2});
    }

    template <typename T>
    constexpr T 
    floor(T x, T guess, T inc)
    {
        return inc < T{8} ? floor2(x, guess, inc) :
            guess + inc <= x ? floor(x, guess + inc, inc) :
            guess + (inc/T{8})*T{7} <= x ? floor(x, guess + (inc/T{8})*T{7}, inc/T{8}) :
            guess + (inc/T{8})*T{6} <= x ? floor(x, guess + (inc/T{8})*T{6}, inc/T{8}) :
            guess + (inc/T{8})*T{5} <= x ? floor(x, guess + (inc/T{8})*T{5}, inc/T{8}) :
            guess + (inc/T{8})*T{4} <= x ? floor(x, guess + (inc/T{8})*T{4}, inc/T{8}) :
            guess + (inc/T{8})*T{3} <= x ? floor(x, guess + (inc/T{8})*T{3}, inc/T{8}) :
            guess + (inc/T{8})*T{2} <= x ? floor(x, guess + (inc/T{8})*T{2}, inc/T{8}) :
            guess + inc/T{8} <= x ? floor(x, guess + inc/T{8}, inc/T{8}) :
            floor(x, guess, inc/T{8});
    }

    template <typename T>
    constexpr T 
    ceil2(T x, T guess, T dec)
    {
        return guess - dec >= x 
            ? ceil2(x, guess - dec, dec) 
            : dec <= T{1} 
                ? guess 
                : ceil2(x, guess, dec/T{2});
    }

    template <typename T>
    constexpr T 
    ceil(T x, T guess, T dec)
    {
        return dec < T{8} ? ceil2(x, guess, dec) :
            guess - dec >= x ? ceil(x, guess - dec, dec) :
            guess - (dec/T{8})*T{7} >= x ? ceil(x, guess - (dec/T{8})*T{7}, dec/T{8}) :
            guess - (dec/T{8})*T{6} >= x ? ceil(x, guess - (dec/T{8})*T{6}, dec/T{8}) :
            guess - (dec/T{8})*T{5} >= x ? ceil(x, guess - (dec/T{8})*T{5}, dec/T{8}) :
            guess - (dec/T{8})*T{4} >= x ? ceil(x, guess - (dec/T{8})*T{4}, dec/T{8}) :
            guess - (dec/T{8})*T{3} >= x ? ceil(x, guess - (dec/T{8})*T{3}, dec/T{8}) :
            guess - (dec/T{8})*T{2} >= x ? ceil(x, guess - (dec/T{8})*T{2}, dec/T{8}) :
            guess - dec/T{8} >= x ? ceil(x, guess - dec/T{8}, dec/T{8}) :
            ceil(x, guess, dec/T{8});
    }
} /* detail */

constexpr double ceil(double x);

constexpr double 
floor(double x)
{
    return x < 0 
        ? -ceil(-x) 
        : x >= 0 
            ? detail::floor(x, 0.0, 
                detail::ipow(2.0, std::numeric_limits<double>::max_exponent-1))
            : throw std::runtime_error("cx::floor");
}
 
constexpr double 
ceil(double x)
{
    return x < 0 
        ? -floor(-x) 
        : x >= 0 
            ? detail::ceil(x, 
                detail::ipow(2.0, std::numeric_limits<double>::max_exponent-1),
                detail::ipow(2.0, std::numeric_limits<double>::max_exponent-1)) 
            : throw std::runtime_error("cx::ceil");
}

constexpr double 
round(double x)
{ return x >= 0 ? floor(x + 0.5) : ceil(x - 0.5); }
  
   
//----------------------------------------------------------------------------
// natural logarithm using
// https://en.wikipedia.org/wiki/Natural_logarithm#High_precision
// domain error occurs if x <= 0
namespace detail {

    template <typename T>
    constexpr T 
    log_iter(T x, T y)
    { return y + T{2} * (x - cx::exp(y)) / (x + cx::exp(y)); }

    template <typename T>
    constexpr T 
    log(T x, T y)
    { return feq(y, log_iter(x, y)) ? y : log(x, log_iter(x, y)); }

    constexpr long double 
    e() { return 2.71828182845904523536l; }

    // For numerical stability, constrain the domain to be x > 0.25 && x < 1024
    // - multiply/divide as necessary. To achieve the desired recursion depth
    // constraint, we need to account for the max double. So we'll divide by
    // e^5. If you want to compute a compile-time log of huge or tiny long
    // doubles, YMMV.

    // if x <= 1, we will multiply by e^5 repeatedly until x > 1
    template <typename T>
    constexpr T 
    logGT(T x)
    {
        return x > T{0.25} 
            ? log(x, T{0}) 
            : logGT<T>(x * e() * e() * e() * e() * e()) - T{5};
    }

    // if x >= 2e10, we will divide by e^5 repeatedly until x < 2e10
    template <typename T>
    constexpr T 
    logLT(T x)
    {
        return x < T{1024} 
            ? log(x, T{0}) 
            : logLT<T>(x / (e() * e() * e() * e() * e())) + T{5};
    }
} /* detail */

template <typename FloatingPoint>
constexpr FloatingPoint 
log( FloatingPoint x,
     typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
{
    return x < 0 
        ? throw std::runtime_error("cx::log") 
        : x >= FloatingPoint{1024} 
            ? detail::logLT(x) 
            : detail::logGT(x);
}

template <typename Integral>
constexpr double 
log( Integral x,
     typename std::enable_if<std::is_integral<Integral>::value>::type* = 0 )
{ return log(static_cast<double>(x)); }

template <typename FloatingPoint>
constexpr FloatingPoint 
log10( FloatingPoint x,
       typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
{ return log(x) / log(FloatingPoint{10}); }

template <typename Integral>
constexpr double 
log10( Integral x,
       typename std::enable_if<std::is_integral<Integral>::value>::type* = 0 )
{ return log10(static_cast<double>(x)); }

template <typename FloatingPoint>
constexpr FloatingPoint 
log2(FloatingPoint x,
    typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0)
{ return log(x) / log(FloatingPoint{ 2 }); }

template <typename Integral>
constexpr double 
log2( Integral x,
      typename std::enable_if<std::is_integral<Integral>::value>::type* = 0 )
{ return log2(static_cast<double>(x)); }

//----------------------------------------------------------------------------
// pow: compute x^y
// a = x^y = (exp(log(x)))^y = exp(log(x)*y)
template <typename FloatingPoint>
constexpr FloatingPoint 
pow( FloatingPoint x, 
     FloatingPoint y,
     typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
{ return exp(log(x)*y); }

template <typename FloatingPoint>
constexpr FloatingPoint 
pow( FloatingPoint x, 
     int y,
     typename std::enable_if<std::is_floating_point<FloatingPoint>::value>::type* = 0 )
{ return detail::ipow(x, y); }

// pow for general arithmetic types
template <typename Arithmetic1, typename Arithmetic2>
constexpr promoted_t<Arithmetic1, Arithmetic2> 
pow( Arithmetic1 x, 
     Arithmetic2 y,
     typename std::enable_if<std::is_arithmetic<Arithmetic1>::value
                             && std::is_arithmetic<Arithmetic2>::value>::type* = 0 )
{
    using P = promoted_t<Arithmetic1, Arithmetic2>;
    return pow(static_cast<P>(x), static_cast<P>(y));
}

template <typename Integral>
constexpr promoted_t<Integral, int> 
pow( Integral x, 
     int y,
     typename std::enable_if<std::is_integral<Integral>::value>::type* = 0 )
{ return detail::ipow(static_cast<double>(x), y); }
   
} /* cx */
