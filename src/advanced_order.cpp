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

#include "../include/advanced_order.hpp"

namespace sob{

/* BASE TICKET */
AdvancedOrderTicket::AdvancedOrderTicket( order_condition condition,
                                          condition_trigger trigger,
                                          OrderParamaters *order1,
                                          OrderParamaters *order2 )
    :
        _condition(condition),
        _trigger(trigger),
        _order1(order1),
        _order2(order2)
    {
    }

AdvancedOrderTicket::AdvancedOrderTicket() // null ticket
    :
        _condition(order_condition::none),
        _trigger(condition_trigger::none),
        _order1(),
        _order2()
    {
    }

AdvancedOrderTicket::AdvancedOrderTicket(const AdvancedOrderTicket& aot)
    :
        _condition( aot._condition ),
        _trigger( aot._trigger ),
        _order1( copy_order(aot._order1) ),
        _order2( copy_order(aot._order2) )
    {
    }

AdvancedOrderTicket::AdvancedOrderTicket(AdvancedOrderTicket&& aot)
    :
        _condition( aot._condition ),
        _trigger( aot._trigger ),
        _order1( std::move(aot._order1) ),
        _order2( std::move(aot._order2) )
    {
    }

AdvancedOrderTicket&
AdvancedOrderTicket::operator=(const AdvancedOrderTicket& aot)
{
    if(*this != aot){
        _condition = aot._condition;
        _trigger = aot._trigger;
        _order1 = std::move( copy_order(aot._order1) );
        _order2 = std::move( copy_order(aot._order2) );
    }
    return *this;
}

AdvancedOrderTicket&
AdvancedOrderTicket::operator=(AdvancedOrderTicket&& aot)
{
    if(*this != aot){
        _condition = aot._condition;
        _trigger = aot._trigger;
        _order1 = std::move(aot._order1);
        _order2 = std::move(aot._order2);
    }
    return *this;
}

bool
AdvancedOrderTicket::operator==(const AdvancedOrderTicket& aot) const
{
    return _condition == aot._condition
            && _trigger == aot._trigger
            && cmp_orders(_order1, aot._order1)
            && cmp_orders(_order2, aot._order2);
}

const AdvancedOrderTicket AdvancedOrderTicket::null;

const condition_trigger AdvancedOrderTicket::default_trigger =
        condition_trigger::fill_partial;



/* ONE CANCELS OTHER */
AdvancedOrderTicketOCO::AdvancedOrderTicketOCO( condition_trigger trigger,
                                                bool is_buy,
                                                size_t size,
                                                double limit,
                                                double stop )
    :
        AdvancedOrderTicket( condition, trigger,
                new OrderParamatersByPrice(is_buy, size, limit, stop) )
    {
        if( size == 0 ){
            throw std::invalid_argument("invalid order size");
        }
    }

AdvancedOrderTicketOCO
AdvancedOrderTicketOCO::build_limit( bool is_buy,
                                     double limit,
                                     size_t sz,
                                     condition_trigger trigger )
{
    if( !limit ){
        throw std::invalid_argument("invalid price");
    }
    return AdvancedOrderTicketOCO(trigger, is_buy, sz, limit, 0.0);
}

AdvancedOrderTicketOCO
AdvancedOrderTicketOCO::build_stop( bool is_buy,
                                    double stop,
                                    size_t sz,
                                    condition_trigger trigger )
{
    if( !stop ){
        throw std::invalid_argument("invalid price");
    }
    return AdvancedOrderTicketOCO(trigger, is_buy, sz, 0.0, stop);
}

AdvancedOrderTicketOCO
AdvancedOrderTicketOCO::build_stop_limit( bool is_buy,
                                          double stop,
                                          double limit,
                                          size_t sz,
                                          condition_trigger trigger )
{
    if( !limit || !stop ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketOCO(trigger, is_buy, sz, limit, stop);
}

const order_condition AdvancedOrderTicketOCO::condition =
        order_condition::one_cancels_other;



/* ONE TRIGGERS OTHER */
AdvancedOrderTicketOTO::AdvancedOrderTicketOTO( condition_trigger trigger,
                                                bool is_buy,
                                                size_t size,
                                                double limit,
                                                double stop )
    :
        AdvancedOrderTicket( condition, trigger,
                new OrderParamatersByPrice(is_buy, size, limit, stop) )
    {
        if( size == 0 ){
            throw std::invalid_argument("invalid order size");
        }
    }

AdvancedOrderTicketOTO
AdvancedOrderTicketOTO::build_market( bool is_buy,
              size_t sz,
              condition_trigger trigger  )
{
    return AdvancedOrderTicketOTO(trigger, is_buy, sz, 0.0, 0.0);
}

AdvancedOrderTicketOTO
AdvancedOrderTicketOTO::build_limit( bool is_buy,
             double limit,
             size_t sz,
             condition_trigger trigger )
{
    if( !limit ){
        throw std::invalid_argument("invalid price");
    }
    return AdvancedOrderTicketOTO(trigger, is_buy, sz, limit, 0.0);
}

AdvancedOrderTicketOTO
AdvancedOrderTicketOTO::build_stop( bool is_buy,
            double stop,
            size_t sz,
            condition_trigger trigger )
{
    if( !stop ){
        throw std::invalid_argument("invalid price");
    }
    return AdvancedOrderTicketOTO(trigger, is_buy, sz, 0.0, stop);
}

AdvancedOrderTicketOTO
AdvancedOrderTicketOTO::build_stop_limit( bool is_buy,
                                          double stop,
                                          double limit,
                                          size_t sz,
                                          condition_trigger trigger )
{
    if( !limit || !stop ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketOTO(trigger, is_buy, sz, limit, stop);
}

const order_condition AdvancedOrderTicketOTO::condition =
        order_condition::one_triggers_other;


/* FILL OR KILL */
const condition_trigger AdvancedOrderTicketFOK::default_trigger =
        condition_trigger::fill_full;

const order_condition AdvancedOrderTicketFOK::condition =
        order_condition::fill_or_kill;


/* BRACKET */
AdvancedOrderTicketBRACKET::AdvancedOrderTicketBRACKET(
        condition_trigger trigger,
        bool is_buy,
        size_t sz,
        double loss_limit,
        double loss_stop,
        double target_limit
        )
    :
        AdvancedOrderTicket( order_condition::bracket, trigger,
                new OrderParamatersByPrice(is_buy, sz, loss_limit, loss_stop),
                new OrderParamatersByPrice(is_buy, sz, target_limit, 0)
                )
    {
    }

AdvancedOrderTicketBRACKET
AdvancedOrderTicketBRACKET::build_sell_stop_limit( double loss_stop,
                                                   double loss_limit,
                                                   double target_limit,
                                                   size_t sz,
                                                   condition_trigger trigger )
{
    if( target_limit <= loss_stop ){
        throw std::invalid_argument("target_limit <= loss_stop");
    }
    if( loss_limit > loss_stop ){
        throw std::invalid_argument("loss_limit > loss_stop");
    }
    if( !loss_stop || !loss_limit || !target_limit ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketBRACKET(trigger, false, sz, loss_limit,
            loss_stop, target_limit);
}

AdvancedOrderTicketBRACKET
AdvancedOrderTicketBRACKET::build_sell_stop( double loss_stop,
                                             double target_limit,
                                             size_t sz,
                                             condition_trigger trigger )
{
    if( target_limit <= loss_stop ){
        throw std::invalid_argument("target_limit <= loss_stop");
    }
    if( !loss_stop || !target_limit ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketBRACKET(trigger, false, sz, 0, loss_stop,
            target_limit);
}

AdvancedOrderTicketBRACKET
AdvancedOrderTicketBRACKET::build_buy_stop_limit( double loss_stop,
                                                  double loss_limit,
                                                  double target_limit,
                                                  size_t sz,
                                                  condition_trigger trigger )
{
    if( target_limit >= loss_stop ){
        throw std::invalid_argument("target_limit >= loss_stop");
    }
    if( loss_limit < loss_stop ){
        throw std::invalid_argument("loss_limit < loss_stop");
    }
    if( !loss_stop || !loss_limit || !target_limit ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketBRACKET(trigger, true, sz, loss_limit,
            loss_stop, target_limit);
}

AdvancedOrderTicketBRACKET
AdvancedOrderTicketBRACKET::build_buy_stop( double loss_stop,
                                            double target_limit,
                                            size_t sz,
                                            condition_trigger trigger )
{
    if( target_limit >= loss_stop ){
        throw std::invalid_argument("target_limit >= loss_stop");
    }
    if( !loss_stop || !target_limit ){
        throw std::invalid_argument("invalid price(s)");
    }
    return AdvancedOrderTicketBRACKET(trigger, true, sz, 0, loss_stop,
            target_limit);
}

const order_condition AdvancedOrderTicketBRACKET::condition =
        order_condition::bracket;


/* TRAILING STOP */
AdvancedOrderTicketTrailingStop::AdvancedOrderTicketTrailingStop(
        size_t nticks,
        order_condition condition,
        condition_trigger trigger
        )
    :
        AdvancedOrderTicket( condition, trigger,
                             new OrderParamatersByNTicks(0,0,0,nticks) )
    {
    }

AdvancedOrderTicketTrailingStop
AdvancedOrderTicketTrailingStop::build(size_t nticks)
{
    if(nticks == 0){
        throw std::invalid_argument("nticks == 0");
    }
    if( nticks > LONG_MAX ){
        throw std::invalid_argument("nticks overflows long");
    }
    return AdvancedOrderTicketTrailingStop(nticks, condition, default_trigger);
}

const order_condition AdvancedOrderTicketTrailingStop::condition =
        order_condition::trailing_stop;

const condition_trigger AdvancedOrderTicketTrailingStop::default_trigger =
        condition_trigger::fill_full;


/* TRAILING BRACKET */
AdvancedOrderTicketTrailingBracket::AdvancedOrderTicketTrailingBracket(
        size_t stop_nticks,
        size_t target_nticks
        )
    :
        AdvancedOrderTicket(condition, default_trigger,
                new OrderParamatersByNTicks(0,0,0,stop_nticks),
                new OrderParamatersByNTicks(0,0,target_nticks,0))
    {
    }

AdvancedOrderTicketTrailingBracket
AdvancedOrderTicketTrailingBracket::build( size_t stop_nticks,
                                           size_t target_nticks )
{
    if( stop_nticks == 0 || target_nticks == 0 ){
        throw std::invalid_argument("nticks == 0");
    }
    if( stop_nticks > LONG_MAX || target_nticks > LONG_MAX ){
        throw std::invalid_argument("nticks overflows long");
    }
    return AdvancedOrderTicketTrailingBracket(stop_nticks, target_nticks);
}

const order_condition AdvancedOrderTicketTrailingBracket::condition =
        order_condition::trailing_bracket;

const condition_trigger AdvancedOrderTicketTrailingBracket::default_trigger =
        condition_trigger::fill_full;


/* ALL OR NOTHING */
const order_condition AdvancedOrderTicketAON::condition =
        order_condition::all_or_nothing;

const condition_trigger AdvancedOrderTicketAON::default_trigger =
        condition_trigger::none;

}; /* sob */



