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

#include <climits>
#include "common.hpp"
#include "order_paramaters.hpp"

// TODO stop-limit version of trailing stop
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
    std::unique_ptr<OrderParamaters> _order1;
    std::unique_ptr<OrderParamaters> _order2;

    static inline std::unique_ptr<OrderParamaters>
    copy_order(const std::unique_ptr<OrderParamaters>& o)
    { return o ? o->copy_new() : nullptr; }

    static inline bool
    cmp_orders(const std::unique_ptr<OrderParamaters>& o1,
               const std::unique_ptr<OrderParamaters>& o2)
    { return (o1 == o2) || (o1 && o2 && (*o1.get() == *o2.get())); }

protected:
    AdvancedOrderTicket( order_condition condition,
                         condition_trigger trigger,
                         OrderParamaters *order1 = nullptr,
                         OrderParamaters *order2 = nullptr);

public:
    static const AdvancedOrderTicket null;
    static const condition_trigger default_trigger;

    AdvancedOrderTicket(); // null ticket

    AdvancedOrderTicket(const AdvancedOrderTicket& aot);

    AdvancedOrderTicket(AdvancedOrderTicket&& aot);

    AdvancedOrderTicket&
    operator=(const AdvancedOrderTicket& aot);

    AdvancedOrderTicket&
    operator=(AdvancedOrderTicket&& aot);

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

    inline const OrderParamaters*
    order1() const
    { return _order1.get(); }

    inline void
    change_order1(const OrderParamaters& order)
    { _order1 = std::move(order.copy_new()); }

    inline const OrderParamaters*
    order2() const
    { return _order2.get(); }

    inline void
    change_order2(const OrderParamaters& order)
    { _order2 = std::move(order.copy_new()); }

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
        if( !limit ){
            throw std::invalid_argument("invalid price");
        }
        return AdvancedOrderTicketOCO(trigger, is_buy, sz, limit, 0.0);
    }

    static inline AdvancedOrderTicketOCO
    build_stop( bool is_buy,
                double stop,
                size_t sz,
                condition_trigger trigger = default_trigger )
    {
        if( !stop ){
            throw std::invalid_argument("invalid price");
        }
        return AdvancedOrderTicketOCO(trigger, is_buy, sz, 0.0, stop);
    }

    static inline AdvancedOrderTicketOCO
    build_stop_limit( bool is_buy,
                      double stop,
                      double limit,
                      size_t sz,
                      condition_trigger trigger = default_trigger )
    {
        if( !limit || !stop ){
            throw std::invalid_argument("invalid price(s)");
        }
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
        if( !limit ){
            throw std::invalid_argument("invalid price");
        }
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, limit, 0.0);
    }

    static inline AdvancedOrderTicketOTO
    build_stop( bool is_buy,
                double stop,
                size_t sz,
                condition_trigger trigger = default_trigger )
    {
        if( !stop ){
            throw std::invalid_argument("invalid price");
        }
        return AdvancedOrderTicketOTO(trigger, is_buy, sz, 0.0, stop);
    }

    static inline AdvancedOrderTicketOTO
    build_stop_limit( bool is_buy,
                      double stop,
                      double limit,
                      size_t sz,
                      condition_trigger trigger = default_trigger )
    {
        if( !limit || !stop ){
            throw std::invalid_argument("invalid price(s)");
        }
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
            AdvancedOrderTicket( order_condition::bracket, trigger,
                    new OrderParamatersByPrice(is_buy, sz, loss_limit, loss_stop),
                    new OrderParamatersByPrice(is_buy, sz, target_limit, 0)
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


/* only using fill_full trigger for now (no mechanism for size update) */
class AdvancedOrderTicketTrailingStop
        : public AdvancedOrderTicket {

protected:
    AdvancedOrderTicketTrailingStop(size_t nticks,
                                    order_condition condition,
                                    condition_trigger trigger)
        :
            AdvancedOrderTicket(condition, trigger,
                    new OrderParamatersByNTicks(0,0,0,nticks))
        {
        }

public:
    static const order_condition condition;
    static const condition_trigger default_trigger;

    static AdvancedOrderTicketTrailingStop
    build(size_t nticks);

};

/* private inheritance from ...TrailingStop is a bit messy, so... */
class AdvancedOrderTicketTrailingBracket
        : public AdvancedOrderTicket{

protected:
    AdvancedOrderTicketTrailingBracket(size_t stop_nticks, size_t target_nticks)
        :
            AdvancedOrderTicket(condition, default_trigger,
                    new OrderParamatersByNTicks(0,0,0,stop_nticks),
                    new OrderParamatersByNTicks(0,0,target_nticks,0))
        {
        }

public:
    static const order_condition condition;
    static const condition_trigger default_trigger;

    static AdvancedOrderTicketTrailingBracket
    build(size_t stop_nticks, size_t target_nticks);
};

}; /* sob */

#endif


