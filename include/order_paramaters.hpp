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

#ifndef JO_SOB_ORDER
#define JO_SOB_ORDER

#include "common.hpp"

namespace sob{

class OrderParamaters{
    bool _is_buy;
    size_t _size;

public:
    constexpr OrderParamaters(bool is_buy, size_t size)
        :
            _is_buy(is_buy),
            _size(size)
        {
        }

    constexpr OrderParamaters()
        : OrderParamaters(0,0)
        {}

    constexpr OrderParamaters(const OrderParamaters& op)
        :
            _is_buy(op._is_buy),
            _size(op._size)
        {
        }

    virtual
    ~OrderParamaters()
    {}

    virtual OrderParamaters*
    copy_new() const = 0;

    inline bool
    is_buy() const
    { return _is_buy; }

    inline size_t
    size() const
    { return _size; }

    bool
    operator==(const OrderParamaters& op) const;

    inline bool
    operator!=(const OrderParamaters& op) const
    { return !(*this == op); }

    virtual
    operator bool() const
    { return _size != 0; }

    virtual inline double
    limit_price() const
    { return 0; }

    virtual inline double
    stop_price() const
    { return 0; }

    virtual inline size_t
    limit_nticks() const
    { return 0; }

    virtual inline size_t
    stop_nticks() const
    { return 0; }

    virtual inline bool
    is_by_price() const
    { return false; }

    virtual inline bool
    is_by_nticks() const
    { return false; }

    virtual bool
    is_market_order() const = 0;

    virtual bool
    is_limit_order() const = 0;

    virtual bool
    is_stop_order() const = 0;

    virtual bool
    is_stop_limit_order() const = 0;

    virtual order_type
    get_order_type() const = 0;
};

template<typename T>
class OrderParamatersGeneric
        : public OrderParamaters {
    T _limit;
    T _stop;

public:
    constexpr OrderParamatersGeneric(bool is_buy, size_t size, T limit, T stop)
        :
            OrderParamaters(is_buy, size),
            _limit(limit),
            _stop(stop)
        {
        }

    constexpr OrderParamatersGeneric()
        :
            OrderParamaters(0,0),
            _limit(0),
            _stop(0)
        {
        }

    constexpr OrderParamatersGeneric(const OrderParamatersGeneric& op)
        :
            OrderParamaters(op),
            _limit(op._limit),
            _stop(op._stop)
        {
        }

    OrderParamaters*
    copy_new() const
    { return new OrderParamatersGeneric(*this); }

    inline T
    limit() const
    { return _limit; }

    inline T
    stop() const
    { return _stop; }

    bool
    operator==(const OrderParamatersGeneric& op) const;

    inline bool
    operator!=(const OrderParamatersGeneric& op) const
    { return !(*this == op); }

    inline
    operator bool() const
    { return OrderParamaters::operator bool() || _stop || _limit; }

    bool
    is_market_order() const
    { return !(_stop || _limit); }

    bool
    is_limit_order() const
    { return _limit && !_stop; }

    bool
    is_stop_order() const
    { return _stop; }

    bool
    is_stop_limit_order() const
    { return _stop && _limit; }

    order_type
    get_order_type() const
    {
        if( !(*this) ){
            return order_type::null;
        }
        if( _stop ){
            return _limit ? order_type::stop_limit : order_type::stop;
        }
        return _limit ? order_type::limit : order_type::market;
    }
};

class OrderParamatersByPrice
        : public OrderParamatersGeneric<double> {
public:
    using OrderParamatersGeneric::OrderParamatersGeneric;

    OrderParamaters*
    copy_new() const
    { return new OrderParamatersByPrice(*this); }

    inline double
    limit_price() const
    { return limit(); }

    inline double
    stop_price() const
    { return stop(); }

    inline bool
    is_by_price() const
    { return true; }
};

class OrderParamatersByNTicks
        : public OrderParamatersGeneric<size_t> {
public:
    using OrderParamatersGeneric::OrderParamatersGeneric;

    OrderParamaters*
    copy_new() const
    { return new OrderParamatersByNTicks(*this); }

    inline size_t
    limit_nticks() const
    { return limit(); }

    inline size_t
    stop_nticks() const
    { return stop(); }

    inline bool
    is_by_nticks() const
    { return true; }
};

}; /* sob */

#endif /* JO_SOB_ORDER */
