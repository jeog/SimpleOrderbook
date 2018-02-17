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

class SimpleOrderbook; /* simpleoderbook.hpp */
struct order_info; /* simpleoderbook.hpp */
class AdvancedOrderTicket; /* advanced_order.hpp */

typedef unsigned long id_type;

typedef std::ratio<1,4> quarter_tick;
typedef std::ratio<1,10> tenth_tick;
typedef std::ratio<1,32> thirty_secondth_tick;
typedef std::ratio<1,100> hundredth_tick;
typedef std::ratio<1,1000> thousandth_tick;
typedef std::ratio<1,10000> ten_thousandth_tick;

typedef std::chrono::steady_clock clock_type;

typedef std::tuple<clock_type::time_point, double, size_t> timesale_entry_type;

enum class order_type {
    null = 0,
    market,
    limit,
    stop,
    stop_limit
};

enum class order_condition {
    none = 0,
    one_cancels_other,
    one_triggers_other,
    fill_or_kill,
    bracket,
    _bracket_active, // <- 'private' (basically an OCO now)
    trailing_stop,
    _trailing_stop_active,
    trailing_bracket,
    _trailing_bracket_active
    // all_or_nothing
};

enum class condition_trigger {
    none = 0,
    fill_partial,
    fill_full
    // fill x% etc.
};

enum class callback_msg{
    cancel = 0,
    fill,
    stop_to_limit, // <- guaranteed before limit insert / fill callback
    stop_to_market,
    trigger_OCO,
    trigger_OTO,
    trigger_BRACKET_open,
    trigger_BRACKET_close,
    trigger_trailing_stop,
    adjust_trailing_stop,
    kill
};

enum class fill_type{
    none = 0,
    partial,
    full,
    immediate_partial,
    immediate_full
};

enum class side_of_market {
    bid = 1,
    ask = -1,
    both = 0
};

enum class side_of_trade {
    buy = 1,
    sell = -1,
    all = 0
};

typedef std::function<void(callback_msg,id_type,id_type,double,size_t)>  order_exec_cb_type;

class OrderParamaters{
    bool _is_buy;
    size_t _size;
    double _limit;
    double _stop;

public:
    OrderParamaters(bool is_buy, size_t size, double limit, double stop)
        :
            _is_buy(is_buy),
            _size(size),
            _limit(limit),
            _stop(stop)
        {
        }

    OrderParamaters()
        : OrderParamaters(0,0,0,0)
        {}

    inline bool
    is_buy() const
    { return _is_buy; }

    inline size_t
    size() const
    { return _size; }

    inline double
    limit() const
    { return _limit; }

    inline double
    stop() const
    { return _stop; }

    bool
    operator==(const OrderParamaters& op) const;

    inline bool
    operator!=(const OrderParamaters& op) const
    { return !(*this == op); }

    inline
    operator bool() const
    { return _size != 0; }

    inline bool
    is_market_order() const
    { return !(_stop || _limit); }

    inline bool
    is_limit_order() const
    { return _limit && !_stop; }

    inline bool
    is_stop_order() const
    { return _stop; }

    inline bool
    is_stop_limit_order() const
    { return _stop && _limit; }

    order_type
    get_order_type() const;
};

std::string to_string(const order_type& ot);
std::string to_string(const callback_msg& cm);
std::string to_string(const side_of_market& s);
std::string to_string(const side_of_trade& s);
std::string to_string(const clock_type::time_point& tp);
std::string to_string(const order_condition& oc);
std::string to_string(const condition_trigger& ct);
std::string to_string(const order_info& oi);
std::string to_string(const OrderParamaters& op);
std::string to_string(const AdvancedOrderTicket& aot);

std::ostream& operator<<(std::ostream& out, const order_type& ot);
std::ostream& operator<<(std::ostream& out, const callback_msg& cm);
std::ostream& operator<<(std::ostream& out, const side_of_market& s);
std::ostream& operator<<(std::ostream& out, const side_of_trade& s);
std::ostream& operator<<(std::ostream& out, const clock_type::time_point& tp);
std::ostream& operator<<(std::ostream& out, const order_condition& oc);
std::ostream& operator<<(std::ostream& out, const condition_trigger& ct);
std::ostream& operator<<(std::ostream& out, const order_info& oi);
std::ostream& operator<<(std::ostream& out, const OrderParamaters& op);
std::ostream& operator<<(std::ostream& out, const AdvancedOrderTicket& op);

#define INLINE_OPERATOR_PLUS_STR(type) \
inline std::string \
operator+(const type& o, std::string s) \
{ return to_string(o) + s; } \
inline std::string \
operator+(std::string s, const type& o) \
{ return s + to_string(o); }

INLINE_OPERATOR_PLUS_STR(order_type);
INLINE_OPERATOR_PLUS_STR(callback_msg);
INLINE_OPERATOR_PLUS_STR(side_of_market);
INLINE_OPERATOR_PLUS_STR(side_of_trade);
INLINE_OPERATOR_PLUS_STR(clock_type::time_point);
INLINE_OPERATOR_PLUS_STR(order_condition);
INLINE_OPERATOR_PLUS_STR(condition_trigger);
INLINE_OPERATOR_PLUS_STR(order_info);
INLINE_OPERATOR_PLUS_STR(OrderParamaters);
INLINE_OPERATOR_PLUS_STR(AdvancedOrderTicket);

class liquidity_exception
        : public std::logic_error{
public:
    const size_t initial_size;
    const size_t remaining_size;
    const id_type order_id;
    liquidity_exception(size_t initial_size,
                        size_t remaining_size,
                        id_type order_id,
                        std::string msg="");
};

template<typename T>
constexpr bool
equal(T l, T r)
{ return l == r; }

template<typename T, typename... TArgs>
constexpr bool
equal(T a, T b, TArgs... c)
{ return equal(a,b) && equal(a, c...); }

}; /* sob */

#endif 


