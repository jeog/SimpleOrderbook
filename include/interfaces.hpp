/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_SOB_INTERFACES
#define JO_SOB_INTERFACES

#include <vector>
#include <map>

#include "common.hpp"

namespace sob{

class UtilityInterface{
    friend SimpleOrderbook;
protected:
    UtilityInterface() {}
    virtual ~UtilityInterface() {}

public:
    virtual double
    tick_size() const = 0;

    virtual double
    price_to_tick(double price) const = 0;

    virtual long long
    ticks_in_range(double lower, double upper) const = 0;

    virtual long long
    ticks_in_range() const = 0;

    virtual unsigned long long
    tick_memory_required(double lower, double upper) const = 0;

    virtual unsigned long long
    tick_memory_required() const = 0;

    virtual bool
    is_valid_price(double price) const = 0;
};

class QueryInterface
        : public UtilityInterface {
    friend SimpleOrderbook;
protected:
    QueryInterface() {}
    virtual ~QueryInterface() {}

public:
    virtual double
    min_price() const = 0;

    virtual double
    max_price() const = 0;

    virtual double 
    bid_price() const = 0;

    virtual double 
    ask_price() const = 0;

    virtual double 
    last_price() const = 0;

    virtual size_t 
    bid_size() const = 0;

    virtual size_t 
    ask_size() const = 0;

    virtual size_t 
    total_bid_size() const = 0;

    virtual size_t 
    total_ask_size() const = 0;

    virtual size_t 
    total_size() const = 0;

    virtual size_t 
    last_size() const = 0;

    virtual unsigned long long 
    volume() const = 0;

    virtual id_type
    last_id() const = 0;

    virtual std::map<double,size_t> 
    bid_depth(size_t depth=8) const = 0;

    virtual std::map<double,size_t> 
    ask_depth(size_t depth=8) const = 0;

    virtual std::map<double,std::pair<size_t, side_of_market>>
    market_depth(size_t depth=8) const = 0;

    /* new elems get put on back i.e beg() == oldest, end() == newest */
    virtual const std::vector<timesale_entry_type>&
    time_and_sales() const = 0;

    virtual order_info_type 
    get_order_info(id_type id, bool search_limits_first=true) const = 0;
};


class LimitInterface
        : public QueryInterface{
    friend SimpleOrderbook;
protected:
    LimitInterface() {}
    virtual ~LimitInterface() {}

public:
    virtual id_type
    insert_limit_order(bool buy, 
                       double limit, 
                       size_t size,
                       order_exec_cb_type exec_cb = nullptr,
                       order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    replace_with_limit_order(id_type id, 
                             bool buy, 
                             double limit,
                             size_t size, 
                             order_exec_cb_type exec_cb = nullptr,
                             order_admin_cb_type admin_cb = nullptr) = 0;

    virtual bool 
    pull_order(id_type id, bool search_limits_first=true) = 0;
};


class FullInterface
        : public LimitInterface{
    friend SimpleOrderbook;
protected:
    FullInterface() {}
    virtual ~FullInterface() {}

public:
    virtual id_type
    insert_market_order(bool buy, 
                        size_t size, 
                        order_exec_cb_type exec_cb = nullptr,
                        order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    insert_stop_order(bool buy, 
                      double stop, 
                      size_t size,
                      order_exec_cb_type exec_cb = nullptr,
                      order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    insert_stop_order(bool buy, 
                      double stop, 
                      double limit,
                      size_t size, 
                      order_exec_cb_type exec_cb = nullptr,
                      order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    replace_with_market_order(id_type id, 
                              bool buy, 
                              size_t size,
                              order_exec_cb_type exec_cb = nullptr,
                              order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    replace_with_stop_order(id_type id, 
                            bool buy, 
                            double stop, 
                            size_t size,
                            order_exec_cb_type exec_cb = nullptr,
                            order_admin_cb_type admin_cb = nullptr) = 0;

    virtual id_type
    replace_with_stop_order(id_type id, 
                            bool buy, 
                            double stop,
                            double limit, 
                            size_t size,
                            order_exec_cb_type exec_cb = nullptr,
                            order_admin_cb_type admin_cb = nullptr) = 0;

    virtual void 
    dump_buy_limits(std::ostream& out = std::cout) const = 0;

    virtual void 
    dump_sell_limits(std::ostream& out = std::cout) const = 0;

    virtual void 
    dump_buy_stops(std::ostream& out = std::cout) const = 0;

    virtual void 
    dump_sell_stops(std::ostream& out = std::cout) const = 0;

};

class ManagementInterface
        : public FullInterface{
    friend SimpleOrderbook;
protected:
    ManagementInterface() {}
    virtual ~ManagementInterface() {}

public:
    virtual void
    dump_internal_pointers(std::ostream& out = std::cout) const = 0;

    virtual void
    grow_book_above(double new_max) = 0;

    virtual void
    grow_book_below(double new_min) = 0;
};

}; /* sob */

#endif 


