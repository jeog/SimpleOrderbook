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

#ifndef JO_SOB_ADVANCED_ORDER
#define JO_SOB_ADVANCED_ORDER

#include "common.hpp"

// TODO BRACKET orders
// TODO TRAILING STOP orders
// TODO AON orders

// TODO change_order_size functionality to support partial_fill brackets

namespace sob{

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

class advanced_order_error
        : public std::invalid_argument {
public:
    advanced_order_error(std::string msg)
        : std::invalid_argument(msg) {}

    advanced_order_error(const std::invalid_argument& e)
        : std::invalid_argument(e) {}
};

class AdvancedOrderTicket{
    order_condition _condition;
    condition_trigger _trigger;
    OrderParamaters _order1;
    OrderParamaters _order2;

public:
    static const AdvancedOrderTicket null;
    static const condition_trigger default_trigger;

    inline order_condition
    condition() const
    { return _condition; }

    inline void
    change_condition(order_condition c)
    { _condition = c; }

    inline condition_trigger
    trigger() const
    { return _trigger; }

    inline void
    change_trigger(condition_trigger t)
    { _trigger = t; }

    inline const OrderParamaters&
    order1() const
    { return _order1; }

    inline void
    change_order1(const OrderParamaters& op)
    { _order1 = op; }

    inline const OrderParamaters&
    order2() const
    { return _order2; }

    inline void
    change_order2(const OrderParamaters& op)
    { _order2 = op; }

    bool
    operator==(const AdvancedOrderTicket& aot) const;

    inline bool
    operator !=(const AdvancedOrderTicket& aot) const
    { return !(*this == aot); }

    inline
    operator bool() const
    { return *this != null; }

protected:
    AdvancedOrderTicket( order_condition condition,
                         condition_trigger trigger,
                         OrderParamaters order1 = OrderParamaters(),
                         OrderParamaters order2 = OrderParamaters());
};

class AdvancedOrderTicketOCO
        : public AdvancedOrderTicket {
protected:
    AdvancedOrderTicketOCO( condition_trigger trigger,
                            bool is_buy,
                            size_t sz,
                            double limit,
                            double stop );
public:
    static const order_condition condition;

    static inline AdvancedOrderTicketOCO
    build_limit( bool is_buy,
                 double limit,
                 size_t sz,
                 condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOCO(trigger, is_buy, sz, limit, 0.0);
    }

    static inline AdvancedOrderTicketOCO
    build_stop( bool is_buy,
                double stop,
                size_t sz,
                condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOCO(trigger, is_buy, sz, 0.0, stop);
    }

    static inline AdvancedOrderTicketOCO
    build_stop_limit( bool is_buy,
                      double stop,
                      double limit,
                      size_t sz,
                      condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOCO(trigger, is_buy, sz, limit, stop);
    }
};

class AdvancedOrderTicketOTO
        : public AdvancedOrderTicket {
protected:
    AdvancedOrderTicketOTO( condition_trigger trigger,
                            bool is_buy,
                            size_t sz,
                            double limit,
                            double stop );
public:
    static const order_condition condition;

    static inline AdvancedOrderTicketOTO
    build_market( bool is_buy,
                  size_t sz,
                  condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, 0.0, 0.0);
    }

    static inline AdvancedOrderTicketOTO
    build_limit( bool is_buy,
                 double limit,
                 size_t sz,
                 condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, limit, 0.0);
    }

    static inline AdvancedOrderTicketOTO
    build_stop( bool is_buy,
                double stop,
                size_t sz,
                condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, 0.0, stop);
    }

    static inline AdvancedOrderTicketOTO
    build_stop_limit( bool is_buy,
                      double stop,
                      double limit,
                      size_t sz,
                      condition_trigger trigger = default_trigger )
    {
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, limit, stop);
    }
};

class AdvancedOrderTicketFOK
        : public AdvancedOrderTicket {
protected:
    AdvancedOrderTicketFOK( condition_trigger trigger)
        : AdvancedOrderTicket( condition, trigger )
        {}
public:
    static const order_condition condition;
    static const condition_trigger default_trigger;

    static inline AdvancedOrderTicketFOK
    build(condition_trigger trigger = default_trigger)
    {
        return AdvancedOrderTicketFOK(trigger);
    }
};
}; /* sob */

#endif


