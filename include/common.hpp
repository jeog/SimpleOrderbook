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
    // bracket
    // fill_or_kill
    // all_or_nothing
    // trailing_stop
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
typedef std::function<void(id_type)> order_admin_cb_type;

/* < order type, is buy, limit price, stop price, size > */
//typedef std::tuple<order_type,bool,double, double, size_t> order_info_type;

struct order_info { // TODO merge with OrderParamaters
    order_type type;
    bool is_buy;
    double limit;
    double stop;
    size_t size;
    operator bool(){ return type != order_type::null; }
};

std::string to_string(const order_type& ot);
std::string to_string(const callback_msg& cm);
std::string to_string(const side_of_market& s);
std::string to_string(const side_of_trade& s);
std::string to_string(const clock_type::time_point& tp);
std::string to_string(const order_info& oi);

std::ostream& operator<<(std::ostream& out, const order_type& ot);
std::ostream& operator<<(std::ostream& out, const callback_msg& cm);
std::ostream& operator<<(std::ostream& out, const side_of_market& s);
std::ostream& operator<<(std::ostream& out, const side_of_trade& s);
std::ostream& operator<<(std::ostream& out, const clock_type::time_point& tp);
std::ostream& operator<<(std::ostream& out, const order_info& oi);

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

#endif 


