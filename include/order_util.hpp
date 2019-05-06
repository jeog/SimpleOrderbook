
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
is_FOK(const order_queue_elem& e)
{ return e.cond == order_condition::fill_or_kill; }

static constexpr bool
is_FOK(const _order_bndl& bndl )
{ return bndl.cond == order_condition::fill_or_kill; }

static constexpr bool
is_AON(const order_queue_elem& e)
{ return e.cond == order_condition::all_or_nothing; }

static constexpr bool
is_AON(const _order_bndl& bndl )
{ return bndl.cond == order_condition::all_or_nothing; }

static constexpr bool
is_not_AON(const _order_bndl& bndl )
{ return bndl.cond != order_condition::all_or_nothing; }

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

static constexpr bool
has_condition_trigger(const order_queue_elem& e)
{ return e.cond_trigger != condition_trigger::none; }

static constexpr bool
has_condition_trigger(const _order_bndl& bndl)
{ return bndl.trigger != condition_trigger::none; }

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
        switch(iwrap.type){
        case chain_iter_wrap::itype::limit:
            return as_price_params(sob, iwrap.p, *(iwrap.l_iter) );
        case chain_iter_wrap::itype::stop:
            return as_price_params(sob, iwrap.p, *(iwrap.s_iter) );
        case chain_iter_wrap::itype::aon_buy:
            return OrderParamatersByPrice(true, iwrap.a_iter->sz,
                sob->_itop(iwrap.p), 0);
        case chain_iter_wrap::itype::aon_sell:
            return OrderParamatersByPrice(false, iwrap.a_iter->sz,
                sob->_itop(iwrap.p), 0);
        };
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

static order_info
as_order_info(bool is_buy,
              double price,
              const aon_bndl& bndl,
              const AdvancedOrderTicket& aot)
{  return order_info(order_type::limit, is_buy, price, 0, bndl.sz, aot); }

template<typename ChainTy>
static order_info
as_order_info( const sob_class *sob,
               id_type id,
               plevel p,
               typename ChainTy::iterator iter,
               bool is_buy )
{ return as_order_info(is_buy, sob->_itop(p), *iter, sob->_bndl_to_aot(*iter)); }

template<typename ChainTy>
static order_info
as_order_info( const sob_class *sob,
               id_type id,
               plevel p,
               typename ChainTy::iterator iter )
{ return as_order_info<ChainTy>(sob, id, p, iter, sob->_is_buy_order(p, *iter)); }


static order_info
as_order_info(const sob_class *sob, id_type id)
{
    try{
        auto& iwrap = sob->_from_cache(id);
        plevel p = iwrap.p;
        switch(iwrap.type){
        case chain_iter_wrap::itype::limit:
            return as_order_info<limit_chain_type>(sob, id, p, iwrap.l_iter);
        case chain_iter_wrap::itype::stop:
            return as_order_info<stop_chain_type>(sob, id, p, iwrap.s_iter);
        case chain_iter_wrap::itype::aon_buy:
            return as_order_info<aon_chain_type>(sob, id, p, iwrap.a_iter, true);
        case chain_iter_wrap::itype::aon_sell:
            return as_order_info<aon_chain_type>(sob, id, p, iwrap.a_iter, false);
        };
    }catch( OrderNotInCache& e ){}

    return order_info();
}

static order_type
as_order_type(const stop_bndl& bndl)
{ return (bndl && bndl.limit) ? order_type::stop_limit : order_type::stop; }

static order_type
as_order_type(const limit_bndl& bndl)
{ return order_type::limit; }

template<typename BndlTy>
static void
dump(std::ostream& out, const BndlTy& bndl, bool is_buy )
{ out << " <" << (is_buy ? "B " : "S ") << bndl.sz
      << (is_AON(bndl) ? " AON #" : "#" ) << bndl.id << "> "; }

static void
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
