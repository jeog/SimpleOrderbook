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
        _order1.reset( copy_order(aot._order1) );
        _order2.reset( copy_order(aot._order2) );
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
    return AdvancedOrderTicketBRACKET(trigger, true, sz, 0, loss_stop,
            target_limit);
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

const AdvancedOrderTicket AdvancedOrderTicket::null;

const condition_trigger AdvancedOrderTicket::default_trigger =
        condition_trigger::fill_partial;

const condition_trigger AdvancedOrderTicketFOK::default_trigger =
        condition_trigger::fill_full;

const condition_trigger AdvancedOrderTicketTrailingStop::default_trigger =
        condition_trigger::fill_full;

const condition_trigger AdvancedOrderTicketTrailingBracket::default_trigger =
        condition_trigger::fill_full;

const order_condition AdvancedOrderTicketOCO::condition =
        order_condition::one_cancels_other;

const order_condition AdvancedOrderTicketOTO::condition =
        order_condition::one_triggers_other;

const order_condition AdvancedOrderTicketFOK::condition =
        order_condition::fill_or_kill;

const order_condition AdvancedOrderTicketTrailingStop::condition =
        order_condition::trailing_stop;

const order_condition AdvancedOrderTicketTrailingBracket::condition =
        order_condition::trailing_bracket;

}; /* sob */



