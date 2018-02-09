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

// TODO BRACKET orders (use change_size())
// TODO TRAILING STOP orders
// TODO AON orders

namespace sob{

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

protected:
    AdvancedOrderTicket( order_condition condition,
                         condition_trigger trigger,
                         OrderParamaters order1 = OrderParamaters(),
                         OrderParamaters order2 = OrderParamaters());

public:
    static const AdvancedOrderTicket null;
    static const condition_trigger default_trigger;

    AdvancedOrderTicket(); // null ticket

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
    { return _condition != order_condition::none; }
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

class AdvancedOrderTicketBRACKET
        : public AdvancedOrderTicket {
protected:
    AdvancedOrderTicketBRACKET( condition_trigger trigger,
                                bool is_buy,
                                size_t sz,
                                double loss_limit,
                                double loss_stop,
                                double target_limit )
        :
            AdvancedOrderTicket( condition, trigger,
                    OrderParamaters(is_buy, sz, loss_limit, loss_stop),
                    OrderParamaters(is_buy, sz, target_limit, 0)
                    )
        {
        }

public:
    static const order_condition condition;

    static AdvancedOrderTicketBRACKET
    build_sell_stop_limit( double loss_stop,
                           double loss_limit,
                           double target_limit,
                           size_t sz,
                           condition_trigger trigger = default_trigger );

    static AdvancedOrderTicketBRACKET
    build_sell_stop( double loss_stop,
                     double target_limit,
                     size_t sz,
                     condition_trigger trigger = default_trigger );

    static AdvancedOrderTicketBRACKET
    build_buy_stop_limit( double loss_stop,
                           double loss_limit,
                           double target_limit,
                           size_t sz,
                           condition_trigger trigger = default_trigger );

    static AdvancedOrderTicketBRACKET
    build_buy_stop( double loss_stop,
                    double target_limit,
                    size_t sz,
                    condition_trigger trigger = default_trigger );
};

}; /* sob */

#endif


