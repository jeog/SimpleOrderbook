
#ifndef INCLUDE_ORDER_UTIL_HPP_
#define INCLUDE_ORDER_UTIL_HPP_


#include "simpleorderbook.hpp"

namespace sob{

namespace detail{

struct order : public sob_types {

static constexpr bool
is_null(const order_queue_elem& e)
{ return e.type == order_type::null; }

static constexpr bool
is_market(const order_queue_elem& e)
{ return e.type == order_type::market; }

static constexpr bool
is_limit(const order_queue_elem& e)
{ return e.type == order_type::limit; }

static constexpr bool
is_limit(const limit_bndl& e)
{ return true; }

static constexpr bool
is_limit(const stop_bndl& e)
{ return false; }

static constexpr bool
is_stop(const order_queue_elem& e)
{ return e.type == order_type::stop || e.type == order_type::stop_limit; }

static constexpr bool
is_stop(const limit_bndl& e)
{ return false; }

static constexpr bool
is_stop(const stop_bndl& e)
{ return true; }

static constexpr bool
is_buy_stop(const limit_bndl& bndl)
{ return false; }

static constexpr bool
is_buy_stop(const stop_bndl& bndl)
{ return bndl.is_buy; }

static constexpr bool
is_advanced(const order_queue_elem& e)
{ return e.cond != order_condition::none; }

static constexpr bool
is_advanced(const _order_bndl& bndl)
{ return bndl.cond != order_condition::none; }

static constexpr bool
is_OCO(const order_queue_elem& e )
{ return e.cond == order_condition::one_cancels_other; }

static constexpr bool
is_OCO(const _order_bndl& bndl )
{ return bndl.cond == order_condition::one_cancels_other; }

static constexpr bool
is_OTO(const order_queue_elem& e )
{ return e.cond == order_condition::one_triggers_other; }

static constexpr bool
is_OTO(const _order_bndl& bndl )
{ return bndl.cond == order_condition::one_triggers_other; }

static constexpr bool
is_bracket(const order_queue_elem& e )
{ return e.cond == order_condition::bracket; }

static constexpr bool
is_bracket(const _order_bndl& bndl )
{ return bndl.cond == order_condition::bracket; }

static constexpr bool
is_active_bracket(const order_queue_elem& e )
{ return e.cond == order_condition::_bracket_active; }

static constexpr bool
is_active_bracket(const _order_bndl& bndl )
{ return bndl.cond == order_condition::_bracket_active; }

static constexpr bool
is_trailing_stop(const order_queue_elem& e )
{ return e.cond == order_condition::trailing_stop; }

static constexpr bool
is_trailing_stop(const _order_bndl& bndl )
{ return bndl.cond == order_condition::trailing_stop; }

static constexpr bool
is_active_trailing_stop(const order_queue_elem& e )
{ return e.cond == order_condition::_trailing_stop_active; }

static constexpr bool
is_active_trailing_stop(const _order_bndl& bndl )
{ return bndl.cond == order_condition::_trailing_stop_active; }

static constexpr bool
is_trailing_bracket(const order_queue_elem& e )
{ return e.cond == order_condition::trailing_bracket; }

static constexpr bool
is_trailing_bracket(const _order_bndl& bndl )
{ return bndl.cond == order_condition::trailing_bracket; }

static constexpr bool
is_active_trailing_bracket(const order_queue_elem& e )
{ return e.cond == order_condition::_trailing_bracket_active; }

static constexpr bool
is_active_trailing_bracket(const _order_bndl& bndl )
{ return bndl.cond == order_condition::_trailing_bracket_active; }

static constexpr bool
needs_partial_fill(const order_queue_elem& e)
{ return e.cond_trigger == condition_trigger::fill_partial; }

static constexpr bool
needs_partial_fill(const _order_bndl& bndl)
{ return bndl.trigger == condition_trigger::fill_partial; }

static constexpr bool
needs_full_fill(const order_queue_elem& e)
{ return e.cond_trigger == condition_trigger::fill_full; }

static constexpr bool
needs_full_fill(const _order_bndl& bndl)
{ return bndl.trigger == condition_trigger::fill_full; }

static constexpr double
index_price(const order_queue_elem& e )
{ return is_limit(e) ? e.limit : e.stop; }

static inline double
limit_price(const sob_class *sob, plevel p, const limit_bndl& bndl)
{ return sob->_itop(p); }

static inline double
limit_price(const sob_class *sob, plevel p, const stop_bndl& bndl)
{ return bndl.limit; }

static inline double
stop_price(const sob_class *sob, plevel p, const limit_bndl& bndl)
{ return 0; }

static inline double
stop_price(const sob_class *sob, plevel p, const stop_bndl& bndl)
{ return sob->_itop(p); }

static inline OrderParamatersByPrice
as_price_params(const sob_class *sob,
                plevel p,
                const stop_bndl& bndl)
{ return OrderParamatersByPrice( is_buy_stop(bndl), bndl.sz,
        limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

static inline OrderParamatersByPrice
as_price_params(const sob_class *sob,
                plevel p,
                const limit_bndl& bndl)
{ return OrderParamatersByPrice( (p < sob->_ask), bndl.sz,
        limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

template<typename ChainTy>
static OrderParamatersByPrice
as_price_params(const sob_class *sob, id_type id)
{
    try{
        auto& iwrap = sob->_from_cache(id);
        return iwrap.is_limit
            ? as_price_params( sob, iwrap.p, *(iwrap.l_iter) )
                : as_price_params( sob, iwrap.p, *(iwrap.s_iter) );
    }catch( OrderNotInCache& e ){}

    return OrderParamatersByPrice();
}

static inline order_info
as_order_info(bool is_buy,
              double price,
              const limit_bndl& bndl,
              const AdvancedOrderTicket& aot)
{ return order_info(order_type::limit, is_buy, price, 0, bndl.sz, aot); }

static order_info
as_order_info(bool is_buy,
              double price,
              const stop_bndl& bndl,
              const AdvancedOrderTicket& aot)
{
    auto ot = bndl.limit ? order_type::stop_limit : order_type::stop;
    return {ot, is_buy, (bndl.limit ? bndl.limit : 0), price, bndl.sz, aot};
}

template<typename ChainTy>
static order_info
as_order_info( const sob_class *sob,
               id_type id,
               plevel p,
               typename ChainTy::iterator iter )
{
    const auto& bndl = *iter;
    AdvancedOrderTicket aot = sob->_bndl_to_aot(bndl);
    bool is_buy = sob->_is_buy_order(p, bndl);
    return as_order_info(is_buy, sob->_itop(p), bndl, aot);
}

static order_info
as_order_info(const sob_class *sob, id_type id)
{
    try{
        auto& iwrap = sob->_from_cache(id);
        return iwrap.is_limit
            ? as_order_info<limit_chain_type>(sob, id, iwrap.p, iwrap.l_iter )
            : as_order_info<stop_chain_type>(sob, id, iwrap.p, iwrap.s_iter );
    }catch( OrderNotInCache& e ){}

    return order_info();
}

static order_type
as_order_type(const stop_bndl& bndl)
{ return (bndl && bndl.limit) ? order_type::stop_limit : order_type::stop; }

static order_type
as_order_type(const limit_bndl& bndl)
{ return order_type::limit; }

static inline void
dump(std::ostream& out, const limit_bndl& bndl, bool is_buy )
{ out << " <" << (is_buy ? "B " : "S ")
              << bndl.sz << " #" << bndl.id << "> "; }

static inline void
dump(std::ostream& out, const stop_bndl& bndl, bool is_buy )
{
    out << " <" << (is_buy ? "B " : "S ") << bndl.sz << " @ "
                << (bndl.limit ? std::to_string(bndl.limit) : "MKT")
                << " #" << bndl.id << "> ";
}

}; /* order */

}; /* detail */

}; /* sob */

#endif /* INCLUDE_ORDER_UTIL_HPP_ */
