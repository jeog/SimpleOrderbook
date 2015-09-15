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


#ifndef JO_0815_INTERFACES
#define JO_0815_INTERFACES

#include <vector>
#include <map>

#include "types.hpp"

namespace NativeLayer{

namespace SimpleOrderbook{

/*
 * (see simple_orderbook.hpp for a complete description)
 */

class QueryInterface{
protected:
  QueryInterface()
    {
    }

public:
  virtual ~QueryInterface()
    {
    }
  typedef QueryInterface my_type;
  typedef typename clock_type::time_point                   time_stamp_type;
  typedef std::tuple<time_stamp_type,price_type,size_type>  t_and_s_type;
  typedef std::vector< t_and_s_type >                       time_and_sales_type;
  typedef std::map<price_type,size_type>                    market_depth_type;
  typedef std::function<my_type*(size_type,size_type,size_type)>  cnstr_type;

  virtual price_type bid_price() const = 0;
  virtual price_type ask_price() const = 0;
  virtual price_type last_price() const = 0;
  virtual size_type bid_size() const = 0;
  virtual size_type ask_size() const = 0;
  virtual size_type last_size() const = 0;
  virtual large_size_type volume() const = 0;
  virtual large_size_type last_id() const = 0;
  virtual market_depth_type bid_depth(size_type depth=8) const = 0;
  virtual market_depth_type ask_depth(size_type depth=8) const = 0;
  virtual market_depth_type market_depth(size_type depth=8) const = 0;
  virtual const time_and_sales_type& time_and_sales() const = 0;

  /* convert time & sales chrono timepoint to str via ctime */
  static std::string timestamp_to_str(const time_stamp_type& tp);
};


class LimitInterface
    : public QueryInterface{
protected:
  LimitInterface()
    {
    }

public:
  virtual ~LimitInterface()
    {
    }
  typedef LimitInterface my_type;
  typedef QueryInterface my_base_type;
  typedef std::function<my_type*(size_type,size_type,size_type)> cnstr_type;

  virtual
  id_type insert_limit_order(bool buy, price_type limit, size_type size,
                             order_exec_cb_type exec_cb,
                             order_admin_cb_type admin_cb = nullptr) = 0;
  virtual
  id_type replace_with_limit_order(id_type id, bool buy, price_type limit,
                                   size_type size, order_exec_cb_type exec_cb,
                                   order_admin_cb_type admin_cb = nullptr) = 0;
  virtual bool pull_order(id_type id, bool search_limits_first=true) = 0;
};


class FullInterface
    : public LimitInterface{
protected:
  FullInterface()
    {
    }

public:
  virtual ~FullInterface()
    {
    }
  typedef FullInterface my_type;
  typedef LimitInterface my_base_type;
  typedef std::function<my_type*(size_type,size_type,size_type)> cnstr_type;

  virtual void add_market_makers(market_makers_type&& mms) = 0;
  virtual void add_market_maker(MarketMaker&& mms) = 0;
  virtual id_type insert_market_order(bool buy, size_type size,
                                    order_exec_cb_type exec_cb,
                                    order_admin_cb_type admin_cb = nullptr) = 0;
  virtual id_type insert_stop_order(bool buy, price_type stop, size_type size,
                                    order_exec_cb_type exec_cb,
                                    order_admin_cb_type admin_cb = nullptr) = 0;
  virtual id_type insert_stop_order(bool buy, price_type stop, price_type limit,
                                    size_type size,
                                    order_exec_cb_type exec_cb,
                                    order_admin_cb_type admin_cb = nullptr) = 0;
  virtual id_type
  replace_with_market_order(id_type id, bool buy, size_type size,
                            order_exec_cb_type exec_cb,
                            order_admin_cb_type admin_cb = nullptr) = 0;
  virtual id_type
  replace_with_stop_order(id_type id, bool buy, price_type stop, size_type size,
                          order_exec_cb_type exec_cb,
                          order_admin_cb_type admin_cb = nullptr) = 0;
  virtual id_type
  replace_with_stop_order(id_type id, bool buy, price_type stop,
                          price_type limit, size_type size,
                          order_exec_cb_type exec_cb,
                          order_admin_cb_type admin_cb = nullptr) = 0;

  virtual void dump_buy_limits() const = 0;
  virtual void dump_sell_limits() const = 0;
  virtual void dump_buy_stops() const = 0;
  virtual void dump_sell_stops() const = 0;
};

};

};

#endif /* JO_0815_INTERFACES */
