/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include "../include/simpleorderbook.hpp"

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

SOB_TEMPLATE
template<side_of_market Side, typename Impl>
struct SOB_CLASS::_high_low{
    typedef typename SOB_CLASS::plevel plevel;

    template<typename ChainTy>
    static void
    get(const Impl* sob, plevel* ph, plevel* pl, size_t depth )
    {
        _get<ChainTy>::get(sob,ph,pl);
        *ph = std::min(sob->_ask + depth - 1, *ph);
        *pl = std::max(sob->_bid - depth + 1, *pl);
    }

    template<typename ChainTy>
    static inline void
    get(const Impl* sob, plevel* ph, plevel *pl)
    { _get<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _get;

    template<typename Dummy>
    struct _get<typename SOB_CLASS::limit_chain_type,Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        {
            *pl = std::min(sob->_low_buy_limit, sob->_ask);
            *ph = std::max(sob->_high_sell_limit, sob->_bid);
        }
    };

    template<typename Dummy>
    struct _get<typename SOB_CLASS::stop_chain_type,Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        {
            *pl = std::min(sob->_low_sell_stop, sob->_low_buy_stop);
            *ph = std::max(sob->_high_sell_stop, sob->_high_buy_stop);
        }
    };
};


SOB_TEMPLATE
template<typename Impl>
struct SOB_CLASS::_high_low<side_of_market::bid,Impl>
        : public _high_low<side_of_market::both,Impl> {
    using typename _high_low<side_of_market::both,Impl>::plevel;

    template<typename ChainTy>
    static void
    get(const Impl* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _get<ChainTy>::get(sob,ph,pl);
        *ph = sob->_bid;
        *pl = std::max(sob->_bid - depth +1, *pl);
    }

    template<typename ChainTy>
    static inline void
    get(const Impl* sob, plevel* ph, plevel *pl)
    { _get<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _get;

    template<typename Dummy>
    struct _get<typename SOB_CLASS::limit_chain_type,Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        {
            *pl = sob->_low_buy_limit;
            *ph = sob->_bid;
        }
    };

    template<typename Dummy>
    struct _get<typename SOB_CLASS::stop_chain_type, Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        { _high_low<side_of_market::both, Impl>::template
              get<typename SOB_CLASS::stop_chain_type>::get(sob,ph,pl); }
    };
};


SOB_TEMPLATE
template<typename Impl>
struct SOB_CLASS::_high_low<side_of_market::ask,Impl>
        : public _high_low<side_of_market::both,Impl> {
    using typename _high_low<side_of_market::both,Impl>::plevel;

    template<typename ChainTy>
    static void
    get(const Impl* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _get<ChainTy>::get(sob,ph,pl);
        *pl = sob->_ask;
        *ph = std::min(sob->_ask + depth - 1, *ph);
    }

    template<typename ChainTy>
    static inline void
    get(const Impl* sob, plevel* ph, plevel *pl)
    { _get<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _get;

    template<typename Dummy>
    struct _get<typename SOB_CLASS::limit_chain_type,Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        {
            *pl = sob->_ask;
            *ph = sob->_high_sell_limit;
        }
    };

    template<typename Dummy>
    struct _get<typename SOB_CLASS::stop_chain_type,Dummy>{
        static inline void
        get(const Impl* sob, plevel* ph, plevel *pl)
        { _high_low<side_of_market::both,Impl>::template
              get<typename SOB_CLASS::stop_chain_type>::get(sob,ph,pl); }
    };
};


/*
 * _order: utilities for individual orders
 */
SOB_TEMPLATE
struct SOB_CLASS::_order {
    template<typename ChainTy>
    static typename ChainTy::value_type&
    find(ChainTy *c, id_type id)
    {
        if( c ){
            auto i = find_pos(c, id);
            if( i != c->cend() ){
                return *i;
            }
        }
        return ChainTy::value_type::null;
    }

    template<typename ChainTy>
    static inline typename ChainTy::value_type&
    find(plevel p, id_type id)
    { return find(_chain<ChainTy>::get(p), id); }

    template<typename ChainTy>
    static typename ChainTy::iterator
    find_pos(ChainTy *c, id_type id)
    {
        return std::find_if(c->begin(), c->end(),
                [&](const typename ChainTy::value_type& v){
                    return v.id == id;
                });
    }

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

    static inline double
    limit_price(const SimpleOrderbookImpl *sob, plevel p, const limit_bndl& bndl)
    { return sob->_itop(p); }

    static inline double
    limit_price(const SimpleOrderbookImpl *sob, plevel p, const stop_bndl& bndl)
    { return bndl.limit; }

    static inline double
    stop_price(const SimpleOrderbookImpl *sob, plevel p, const limit_bndl& bndl)
    { return 0; }

    static inline double
    stop_price(const SimpleOrderbookImpl *sob, plevel p, const stop_bndl& bndl)
    { return sob->_itop(p); }

    static inline OrderParamaters
    as_order_params(const SimpleOrderbookImpl *sob,
                    plevel p,
                    const stop_bndl& bndl)
    { return OrderParamaters( is_buy_stop(bndl), bndl.sz,
            limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

    static inline OrderParamaters
    as_order_params(const SimpleOrderbookImpl *sob,
                    plevel p,
                    const limit_bndl& bndl)
    { return OrderParamaters( (p < sob->_ask), bndl.sz,
            limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

    static inline order_info
    as_order_info(bool is_buy,
                  double price,
                  const limit_bndl& bndl,
                  const AdvancedOrderTicket& aot)
    { return {order_type::limit, is_buy, price, 0, bndl.sz, aot}; }

    static order_info
    as_order_info(bool is_buy,
                  double price,
                  const stop_bndl& bndl,
                  const AdvancedOrderTicket& aot)
    {
        if( !bndl.limit ){
            return {order_type::stop, is_buy, 0, price, bndl.sz, aot};
        }
        return {order_type::stop_limit, is_buy, bndl.limit, price, bndl.sz, aot };
    }

    /* return an order_info struct for that order id */
    template<typename ChainTy>
    static order_info
    as_order_info(const SimpleOrderbookImpl *sob, id_type id)
    {
        plevel p = sob->_id_to_plevel<ChainTy>(id);
        if( !p ){
            return {order_type::null, false, 0, 0, 0, AdvancedOrderTicket::null};
        }
        ChainTy *c = _chain<ChainTy>::get(p);
        const typename ChainTy::value_type& bndl = _order::find(c, id);
        AdvancedOrderTicket aot =  sob->_bndl_to_aot<ChainTy>(bndl);
        bool is_buy = sob->_is_buy_order(p, bndl);
        return as_order_info(is_buy, sob->_itop(p), bndl, aot);
    }

    template<typename PrimaryChainTy, typename SecondaryChainTy>
    static order_info
    as_order_info(const SimpleOrderbookImpl *sob, id_type id)
    {
        auto oi =  as_order_info<PrimaryChainTy>(sob, id);
        if( !oi ){
            oi = as_order_info<SecondaryChainTy>(sob, id);
        }
        return oi;
    }

    static constexpr order_type
    as_order_type(const stop_bndl& bndl)
    { return (bndl && bndl.limit) ? order_type::stop_limit : order_type::stop; }

    static constexpr order_type
    as_order_type(const limit_bndl& bndl)
    { return order_type::limit; }

    static inline void
    dump(std::ostream& out, const SOB_CLASS::limit_bndl& bndl )
    { out << " <" << bndl.sz << " #" << bndl.id << "> "; }

    static inline void
    dump(std::ostream& out, const SOB_CLASS::stop_bndl& bndl )
    {
        out << " <" << (bndl.is_buy ? "B " : "S ")
                    << bndl.sz << " @ "
                    << (bndl.limit ? std::to_string(bndl.limit) : "MKT")
                    << " #" << bndl.id << "> ";
    }
};



SOB_TEMPLATE
template<typename ChainTy, typename Dummy>
struct SOB_CLASS::_chain {
    static constexpr bool is_limit = false;
    static constexpr bool is_stop = false;

protected:
    template<typename InnerChainTy>
    static size_t
    size(InnerChainTy *c)
    {
        size_t sz = 0;
        for( const typename InnerChainTy::value_type& e : *c ){
            sz += e.sz;
        }
        return sz;
    }

    template<typename InnerChainTy>
    static typename InnerChainTy::value_type
    pop(SOB_CLASS *sob, InnerChainTy *c, id_type id)
    {
        auto i = _order::template find_pos(c, id);
        if( i == c->cend() ){
            return InnerChainTy::value_type::null;
        }
        typename InnerChainTy::value_type bndl = *i;
        c->erase(i);
        sob->_id_cache.erase(id);
        return bndl;
    }
};


SOB_TEMPLATE
template<typename Dummy>
struct SOB_CLASS::_chain<typename SOB_CLASS::limit_chain_type, Dummy>
        : public _chain<void> {
    typedef typename SOB_CLASS::limit_chain_type chain_type;
    typedef typename SOB_CLASS::limit_bndl bndl_type;
    typedef typename SOB_CLASS::plevel plevel;

    static constexpr bool is_limit = true;

    static constexpr chain_type*
    get(plevel p)
    { return &(p->first); }

    static inline size_t
    size(chain_type* c)
    { return _chain<void>::template size(c); }

    static constexpr order_type
    as_order_type()
    { return SOB_CLASS::_order::as_order_type(bndl_type()); }

    template<bool IsBuy>
    static void
    push(SOB_CLASS *sob, plevel p, bndl_type&& bndl)
    {
        sob->_id_cache[bndl.id] = std::make_pair(sob->_itop(p), true);
        chain_type *c = &p->first;
        c->emplace_back(bndl); /* moving bndl */
        SOB_CLASS::_limit_exec<IsBuy>::adjust_state_after_insert(sob, p, c);
    }

    static bndl_type
    pop(SOB_CLASS *sob, plevel p, id_type id)
    {
        chain_type *c = &p->first;
        bndl_type bndl = _chain<void>::template pop(sob,c,id);
        if( bndl && c->empty() ){
             /*  we can compare vs bid because if we get here and the order is
                 a buy it must be <= the best bid, otherwise its a sell

                (remember, p is empty if we get here)  */
             (p <= sob->_bid)
                 ? SOB_CLASS::_limit_exec<true>::adjust_state_after_pull(sob, p)
                 : SOB_CLASS::_limit_exec<false>::adjust_state_after_pull(sob, p);
        }
        return bndl;
    }
};


SOB_TEMPLATE
template<typename Dummy>
struct SOB_CLASS::_chain<typename SOB_CLASS::stop_chain_type, Dummy>
        : public _chain<void> {
    typedef typename SOB_CLASS::stop_chain_type chain_type;
    typedef typename SOB_CLASS::stop_bndl bndl_type;
    typedef typename SOB_CLASS::plevel plevel;

    static constexpr bool is_stop = true;

    static constexpr chain_type*
    get(plevel p)
    { return &(p->second); }

    static inline size_t
    size(chain_type* c)
    { return _chain<void>::template size(c); }

    static constexpr order_type
    as_order_type() // doesn't differentiate between stop & stop/limit
    { return SOB_CLASS::_order::as_order_type(bndl_type()); }

    template<bool IsBuy>
    static void
    push(SOB_CLASS *sob, plevel p, bndl_type&& bndl)
    {
        sob->_id_cache[bndl.id] = std::make_pair(sob->_itop(p),false);
        p->second.emplace_back(bndl); /* moving bndl */
        SOB_CLASS::_stop_exec<IsBuy>::adjust_state_after_insert(sob, p);
    }

    static bndl_type
    pop(SOB_CLASS *sob, SOB_CLASS::plevel p, id_type id)
    {
        chain_type *c = &p->second;
        bndl_type bndl = _chain<void>::template pop(sob, c, id);
        if( !bndl ){
            return bndl;
        }
        if( SOB_CLASS::_order::is_buy_stop(bndl) ){
            if( SOB_CLASS::_stop_exec<true>::stop_chain_is_empty(sob, c) ){
                SOB_CLASS::_stop_exec<true>::adjust_state_after_pull(sob, p);
            }
        }else{
            if( SOB_CLASS::_stop_exec<false>::stop_chain_is_empty(sob, c) ){
                SOB_CLASS::_stop_exec<false>::adjust_state_after_pull(sob, p);
            }
        }
        return bndl;
    }
};


SOB_TEMPLATE
template<side_of_market Side, typename Impl>
struct SOB_CLASS::_depth{
    typedef size_t mapped_type;

    static inline
    size_t
    build_value(const Impl* sob, typename SOB_CLASS::plevel p, size_t d){
        return d;
    }
};

SOB_TEMPLATE
template<typename Impl>
struct SOB_CLASS::_depth<side_of_market::both, Impl>{
    typedef std::pair<size_t, side_of_market> mapped_type;

    static inline
    mapped_type
    build_value(const Impl* sob, typename SOB_CLASS::plevel p, size_t d){
        auto s = (p >= sob->_ask) ? side_of_market::ask : side_of_market::bid;
        return std::make_pair(d,s);
    }
};


SOB_TEMPLATE
template<bool BidSide, bool Redirect, typename Impl> /* SELL, hit bids */
struct SOB_CLASS::_core_exec {
    static inline bool
    is_available(const Impl *sob)
    { return (sob->_bid >= sob->_beg); }

    static inline bool
    is_executable_chain(const Impl* sob, typename SOB_CLASS::plevel p)
    { return (p <= sob->_bid || !p) && is_available(sob); }

    static inline typename SOB_CLASS::plevel
    get_inside(const Impl* sob)
    { return sob->_bid; }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    static bool
    find_new_best_inside(Impl* sob)
    {
        /* if on an empty chain 'jump' to next that isn't, reset _ask as we go */
        _core_exec<Redirect>::_jump_to_nonempty_chain(sob);
        /* reset size; if we run out of orders reset state/cache and return */
        if( !_core_exec<Redirect>::_check_and_reset(sob) ){
            return false;
        }
        _core_exec<Redirect>::_adjust_limit_pointer(sob);
        return true;
    }

private:
    static inline void
    _jump_to_nonempty_chain(Impl* sob)
    {
        for( ;
             sob->_bid->first.empty() && (sob->_bid >= sob->_beg);
             --sob->_bid )
           {
           }
    }

    static bool
    _check_and_reset(Impl* sob)
    {
        if(sob->_bid < sob->_beg){
            sob->_low_buy_limit = sob->_end;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(Impl* sob)
    {
        if( sob->_bid < sob->_low_buy_limit ){
            sob->_low_buy_limit = sob->_bid;
        }
    }
};


SOB_TEMPLATE
template<bool Redirect, typename Impl> /* BUY, hit offers */
struct SOB_CLASS::_core_exec<false, Redirect, Impl>
        : public _core_exec<true,false,Impl> {
    friend _core_exec<true,false,Impl>;

    static inline bool
    is_available(const Impl *sob)
    { return (sob->_ask < sob->_end); }

    static inline bool
    is_executable_chain(const Impl* sob, typename SOB_CLASS::plevel p)
    { return (p >= sob->_ask || !p) && is_available(sob); }

    static inline typename SOB_CLASS::plevel
    get_inside(const Impl* sob)
    { return sob->_ask; }

private:
    static inline void
    _jump_to_nonempty_chain(Impl* sob)
    {
        for( ;
             sob->_ask->first.empty() && (sob->_ask < sob->_end);
             ++sob->_ask )
            {
            }
    }

    static bool
    _check_and_reset(Impl* sob)
    {
        if( sob->_ask >= sob->_end ){
            sob->_high_sell_limit = sob->_beg - 1;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(Impl* sob)
    {
        if( sob->_ask > sob->_high_sell_limit ){
            sob->_high_sell_limit = sob->_ask;
        }
    }
};


SOB_TEMPLATE
template<bool BuyLimit, typename Impl>
struct SOB_CLASS::_limit_exec {
    static void
    adjust_state_after_pull(Impl *sob, SOB_CLASS::plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit >= sob->_low_buy_limit );
        assert( limit <= sob->_bid );
        if( limit == sob->_low_buy_limit ){
            ++sob->_low_buy_limit;
        }
        if( limit == sob->_bid ){
            SOB_CLASS::_core_exec<true>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(Impl *sob,
                              SOB_CLASS::plevel limit,
                              SOB_CLASS::limit_chain_type* orders)
    {
        assert( limit >= sob->_beg );
        assert( limit < sob->_end );
        if( limit >= sob->_bid ){
            sob->_bid = limit;
        }
        if( limit < sob->_low_buy_limit ){
            sob->_low_buy_limit = limit;
        }
    }

    static inline bool /* slingshot back to protected fillable */
    fillable(Impl *sob, SOB_CLASS::plevel p, size_t sz, bool is_buy)
    { return is_buy ? _limit_exec<true>::fillable(sob, p, sz)
                    : _limit_exec<false>::fillable(sob, p, sz); }

    static inline bool
    fillable(Impl *sob, SOB_CLASS::plevel p, size_t sz)
    { return (sob->_ask < sob->_end) && fillable(sob->_ask, p, sz); }

protected:
    static bool
    fillable(SOB_CLASS::plevel l, SOB_CLASS::plevel h, size_t sz)
    {
        size_t tot = 0;
        for( ; l <= h; ++l){
            for( const auto& e : *_chain<limit_chain_type>::get(l) ){
                tot += e.sz;
                if( tot >= sz ){
                    return true;
                }
            }
        }
        return false;
    }
};


SOB_TEMPLATE
template<typename Impl>
struct SOB_CLASS::_limit_exec<false, Impl>
        : public _limit_exec<true, Impl>{
    static void
    adjust_state_after_pull(Impl *sob, SOB_CLASS::plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit <= sob->_high_sell_limit );
        assert( limit >= sob->_ask );
        if( limit == sob->_high_sell_limit ){
            --sob->_high_sell_limit;
        }
        if( limit == sob->_ask ){
            SOB_CLASS::_core_exec<false>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(Impl *sob,
                              SOB_CLASS::plevel limit,
                              SOB_CLASS::limit_chain_type* orders)
    {
        assert( limit >= sob->_beg );
        assert( limit < sob->_end );
        if( limit <= sob->_ask) {
            sob->_ask = limit;
        }
        if( limit > sob->_high_sell_limit ){
            sob->_high_sell_limit = limit;
        }
    }

    static inline bool
    fillable(Impl *sob, SOB_CLASS::plevel p, size_t sz)
    { return (sob->_bid >= sob->_beg)
              && _limit_exec<true>::fillable(p, sob->_bid, sz); }
};


// TODO mechanism to jump to new stop cache vals ??
SOB_TEMPLATE
template<bool BuyStop, bool Redirect, typename Impl>
struct SOB_CLASS::_stop_exec {
    static void
    adjust_state_after_pull(Impl* sob, SOB_CLASS::plevel stop)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( stop <= sob->_high_buy_stop );
        assert( stop >= sob->_low_buy_stop );
        if( sob->_high_buy_stop == sob->_low_buy_stop ){
            /* last order, reset to 'null' levels */
            assert(stop == sob->_high_buy_stop);
            sob->_high_buy_stop = sob->_beg - 1;
            sob->_low_buy_stop = sob->_end;
        }else if( stop == sob->_high_buy_stop ){
            --sob->_high_buy_stop;
        }else if( stop == sob->_low_buy_stop ){
            ++sob->_low_buy_stop;
        }
    }

    static void
    adjust_state_after_insert(Impl* sob, SOB_CLASS::plevel stop)
    {
        if( stop < sob->_low_buy_stop ){
            sob->_low_buy_stop = stop;
        }
        if( stop > sob->_high_buy_stop ){
            sob->_high_buy_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(Impl* sob, SOB_CLASS::plevel stop)
    {
        sob->_low_buy_stop = stop + 1;
        if( sob->_low_buy_stop > sob->_high_buy_stop ){
            sob->_low_buy_stop = sob->_end;
            sob->_high_buy_stop = sob->_beg - 1;
        }
    }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    static bool
    stop_chain_is_empty(Impl* sob, SOB_CLASS::stop_chain_type* c)
    {
        static auto ifcond =
            [](const typename SOB_CLASS::stop_chain_type::value_type & v){
                return v.is_buy == Redirect;
            };
        auto biter = c->cbegin();
        auto eiter = c->cend();
        auto riter = find_if(biter, eiter, ifcond);
        return (riter == eiter);
    }
};


SOB_TEMPLATE
template<bool Redirect, typename Impl>
struct SOB_CLASS::_stop_exec<false, Redirect, Impl>
        : public _stop_exec<true, false, Impl> {
    static void
    adjust_state_after_pull(Impl* sob, SOB_CLASS::plevel stop)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( stop <= sob->_high_sell_stop );
        assert( stop >= sob->_low_sell_stop );
        if( sob->_high_sell_stop == sob->_low_sell_stop ){
            assert(stop == sob->_high_sell_stop);
            sob->_high_sell_stop = sob->_beg - 1;
            sob->_low_sell_stop = sob->_end;
        }else if( stop == sob->_high_sell_stop ){
            --sob->_high_sell_stop;
        }else if( stop == sob->_low_sell_stop ){
            ++sob->_low_sell_stop;
        }
    }

    static void
    adjust_state_after_insert(Impl* sob, SOB_CLASS::plevel stop)
    {
        if( stop > sob->_high_sell_stop ){
            sob->_high_sell_stop = stop;
        }
        if( stop < sob->_low_sell_stop ){
            sob->_low_sell_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(Impl* sob, SOB_CLASS::plevel stop)
    {
        sob->_high_sell_stop = stop - 1;
        if( sob->_high_sell_stop < sob->_low_sell_stop ){
            sob->_high_sell_stop = sob->_beg - 1;
            sob->_low_sell_stop = sob->_end;
        }
    }
};

};

#undef SOB_TEMPLATE
#undef SOB_CLASS
