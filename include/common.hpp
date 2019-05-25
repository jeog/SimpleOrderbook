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
class OrderParamaters; /* order_paramaters.hpp */
class AdvancedOrderTicket; /* advanced_order.hpp */
struct order_info; /* simpleoderbook.hpp */

using id_type = unsigned long;

using quarter_tick = std::ratio<1,4>;
using tenth_tick = std::ratio<1,10>;
using thirty_secondth_tick = std::ratio<1,32>;
using hundredth_tick = std::ratio<1,100>;
using thousandth_tick = std::ratio<1,1000>;
using ten_thousandth_tick = std::ratio<1,10000>;

using clock_type = std::chrono::steady_clock;

using timesale_entry_type = std::tuple<clock_type::time_point, double, size_t>;

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
    _bracket_active, // private
    trailing_stop,
    _trailing_stop_active, // private
    trailing_bracket,
    _trailing_bracket_active, // private
    all_or_none
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
    stop_to_limit,
    stop_to_market,
    trigger_OCO,
    trigger_OTO,
    trigger_BRACKET_open,
    trigger_BRACKET_open_target,
    trigger_BRACKET_open_loss,
    trigger_BRACKET_adj_target,
    trigger_BRACKET_adj_loss,
    trigger_BRACKET_close,
    trigger_TRAILING_STOP_open,
    trigger_TRAILING_STOP_open_loss,
    trigger_TRAILING_STOP_adj_loss,
    trigger_TRAILING_STOP_close,
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
    both = 0
};

using order_exec_cb_type = std::function<
    void(callback_msg,id_type,id_type,double,size_t)
    >;

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
std::string to_string(const timesale_entry_type& entry);

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
std::ostream& operator<<(std::ostream& out, const timesale_entry_type& entry);

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
INLINE_OPERATOR_PLUS_STR(timesale_entry_type);

class liquidity_exception
        : public std::logic_error{
    size_t _initial_size;
    size_t _remaining_size;
    id_type _order_id;
public:
    liquidity_exception(size_t initial_size,
                        size_t remaining_size,
                        id_type order_id,
                        std::string msg="");
    size_t initital_size() const { return _initial_size; }
    size_t remaining_size() const { return _remaining_size; }
    id_type order_id() const { return _order_id; }

};

class derived_price_exception
        : public std::out_of_range{
public:
    using std::out_of_range::out_of_range;
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


