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

#define SOB_CLASS SimpleOrderbook::SimpleOrderbookBase

namespace sob{

template<side_of_market Side>
struct SOB_CLASS::_high_low{
    template<typename ChainTy>
    static void
    get(const SOB_CLASS* sob, plevel* ph, plevel* pl, size_t depth )
    {
        _helper<ChainTy>::get(sob,ph,pl);
        *ph = std::min(sob->_ask + depth - 1, *ph);
        *pl = std::max(sob->_bid - depth + 1, *pl);
    }

    template<typename ChainTy>
    static inline void
    get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
    { _helper<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        {
            *pl = std::min(sob->_low_buy_limit, sob->_ask);
            *ph = std::max(sob->_high_sell_limit, sob->_bid);
        }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        {
            *pl = std::min(sob->_low_sell_stop, sob->_low_buy_stop);
            *ph = std::max(sob->_high_sell_stop, sob->_high_buy_stop);
        }
    };
};

template<>
struct SOB_CLASS::_high_low<side_of_market::bid>
        : public _high_low<side_of_market::both> {
    template<typename ChainTy>
    static void
    get(const SOB_CLASS* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _helper<ChainTy>::get(sob,ph,pl);
        *ph = sob->_bid;
        *pl = std::max(sob->_bid - depth +1, *pl);
    }

    template<typename ChainTy>
    static inline void
    get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
    { _helper<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        {
            *pl = sob->_low_buy_limit;
            *ph = sob->_bid;
        }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        { _high_low<side_of_market::both>::template
              get<stop_chain_type>(sob,ph,pl); }
    };
};

template<>
struct SOB_CLASS::_high_low<side_of_market::ask>
        : public _high_low<side_of_market::both> {
    template<typename ChainTy>
    static void
    get(const SOB_CLASS* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _helper<ChainTy>::get(sob,ph,pl);
        *pl = sob->_ask;
        *ph = std::min(sob->_ask + depth - 1, *ph);
    }

    template<typename ChainTy>
    static inline void
    get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
    { _helper<ChainTy>::get(sob,ph,pl); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        {
            *pl = sob->_ask;
            *ph = sob->_high_sell_limit;
        }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline void
        get(const SOB_CLASS* sob, plevel* ph, plevel *pl)
        { _high_low<side_of_market::both>::template
              get<stop_chain_type>(sob,ph,pl); }
    };
};


struct SOB_CLASS::_order {

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
    limit_price(const SOB_CLASS *sob, plevel p, const limit_bndl& bndl)
    { return sob->_itop(p); }

    static inline double
    limit_price(const SOB_CLASS *sob, plevel p, const stop_bndl& bndl)
    { return bndl.limit; }

    static inline double
    stop_price(const SOB_CLASS *sob, plevel p, const limit_bndl& bndl)
    { return 0; }

    static inline double
    stop_price(const SOB_CLASS *sob, plevel p, const stop_bndl& bndl)
    { return sob->_itop(p); }

    static inline OrderParamatersByPrice
    as_price_params(const SOB_CLASS *sob,
                    plevel p,
                    const stop_bndl& bndl)
    { return OrderParamatersByPrice( is_buy_stop(bndl), bndl.sz,
            limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

    static inline OrderParamatersByPrice
    as_price_params(const SOB_CLASS *sob,
                    plevel p,
                    const limit_bndl& bndl)
    { return OrderParamatersByPrice( (p < sob->_ask), bndl.sz,
            limit_price(sob, p, bndl), stop_price(sob, p, bndl) ); }

    template<typename ChainTy>
    static OrderParamatersByPrice
    as_price_params(const SOB_CLASS *sob, id_type id)
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
        if( !bndl.limit ){
            return order_info(order_type::stop, is_buy, 0, price, bndl.sz, aot);
        }
        return order_info(order_type::stop_limit, is_buy, bndl.limit, price,
                          bndl.sz, aot);
    }

    /* return an order_info struct for that order id */
    template<typename ChainTy>
    static order_info
    as_order_info( const SOB_CLASS *sob, 
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
    as_order_info(const SOB_CLASS *sob, id_type id)
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
};


template<side_of_market Side>
struct SOB_CLASS::_depth{
    typedef size_t mapped_type;

    static inline size_t
    build_value(const SOB_CLASS* sob, plevel p, size_t d)
    { return d; }
};

template<>
struct SOB_CLASS::_depth<side_of_market::both>{
    typedef std::pair<size_t, side_of_market> mapped_type;

    static inline mapped_type
    build_value(const SOB_CLASS* sob, plevel p, size_t d)
    {
        auto s = (p >= sob->_ask) ? side_of_market::ask : side_of_market::bid;
        return std::make_pair(d,s);
    }
};


template<typename ChainTy, bool BaseOnly>
struct SOB_CLASS::_chain {
    typedef ChainTy chain_type;

    static constexpr bool is_limit = std::is_same<ChainTy, limit_chain_type>::value;
    static constexpr bool is_stop = std::is_same<ChainTy, stop_chain_type>::value;

    static size_t
    size(ChainTy *c)
    {
        size_t sz = 0;
        for( const typename ChainTy::value_type& e : *c ){
            sz += e.sz;
        }
        return sz;
    }
};

template<>
struct SOB_CLASS::_chain<typename SOB_CLASS::limit_chain_type, false>
         : public _chain<typename SOB_CLASS::limit_chain_type, true>{
    typedef limit_bndl bndl_type;

    static constexpr limit_chain_type*
    get(plevel p)
    { return &(p->first); }

    static constexpr order_type
    as_order_type()
    { return order_type::limit; }
};

template<>
struct SOB_CLASS::_chain<typename SOB_CLASS::stop_chain_type, false>
        : public _chain<typename SOB_CLASS::stop_chain_type, true>{
    typedef stop_bndl bndl_type;

    static constexpr stop_chain_type*
    get(plevel p)
    { return &(p->second); }

    /* doesn't differentiate between stop & stop/limit */
    static constexpr order_type
    as_order_type()
    { return order_type::stop; }
};


template<bool BidSide, bool Redirect> /* SELL, hit bids */
struct SOB_CLASS::_core_exec {
    static inline bool
    is_available(const SOB_CLASS *sob)
    { return (sob->_bid >= sob->_beg); }

    static inline bool
    is_executable_chain(const SOB_CLASS *sob, plevel p)
    { return (p <= sob->_bid || !p) && is_available(sob); }

    static inline plevel
    get_inside(const SOB_CLASS* sob)
    { return sob->_bid; }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    static bool
    find_new_best_inside(SOB_CLASS* sob)
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
    _jump_to_nonempty_chain(SOB_CLASS* sob)
    {
        for( ;
             sob->_bid->first.empty() && (sob->_bid >= sob->_beg);
             --sob->_bid )
           {
           }
    }

    static bool
    _check_and_reset(SOB_CLASS* sob)
    {
        if(sob->_bid < sob->_beg){
            sob->_low_buy_limit = sob->_end;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(SOB_CLASS* sob)
    {
        if( sob->_bid < sob->_low_buy_limit ){
            sob->_low_buy_limit = sob->_bid;
        }
    }
};

template<bool Redirect> /* BUY, hit offers */
struct SOB_CLASS::_core_exec<false, Redirect>
        : public _core_exec<true, false> {
    friend _core_exec<true,false>;

    static inline bool
    is_available(const SOB_CLASS *sob)
    { return (sob->_ask < sob->_end); }

    static inline bool
    is_executable_chain(const SOB_CLASS* sob, plevel p)
    { return (p >= sob->_ask || !p) && is_available(sob); }

    static inline plevel
    get_inside(const SOB_CLASS* sob)
    { return sob->_ask; }

private:
    static inline void
    _jump_to_nonempty_chain(SOB_CLASS *sob)
    {
        for( ;
             sob->_ask->first.empty() && (sob->_ask < sob->_end);
             ++sob->_ask )
            {
            }
    }

    static bool
    _check_and_reset(SOB_CLASS *sob)
    {
        if( sob->_ask >= sob->_end ){
            sob->_high_sell_limit = sob->_beg - 1;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(SOB_CLASS *sob)
    {
        if( sob->_ask > sob->_high_sell_limit ){
            sob->_high_sell_limit = sob->_ask;
        }
    }
};


template<bool BuyLimit>
struct SOB_CLASS::_limit_exec {
    static void
    adjust_state_after_pull(SOB_CLASS *sob, plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit >= sob->_low_buy_limit );
        assert( limit <= sob->_bid );
        if( limit == sob->_low_buy_limit ){
            ++sob->_low_buy_limit;
        }
        if( limit == sob->_bid ){
            _core_exec<true>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(SOB_CLASS *sob, plevel limit, limit_chain_type* orders)
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
};

template<>
struct SOB_CLASS::_limit_exec<false>
        : public _limit_exec<true>{
    static void
    adjust_state_after_pull(SOB_CLASS *sob, plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit <= sob->_high_sell_limit );
        assert( limit >= sob->_ask );
        if( limit == sob->_high_sell_limit ){
            --sob->_high_sell_limit;
        }
        if( limit == sob->_ask ){
            _core_exec<false>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(SOB_CLASS *sob, plevel limit, limit_chain_type* orders)
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
};


// TODO mechanism to jump to new stop cache vals ??

template<bool BuyStop, bool Redirect>
struct SOB_CLASS::_stop_exec {
    static void
    adjust_state_after_pull(SOB_CLASS *sob, plevel stop)
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
    adjust_state_after_insert(SOB_CLASS *sob, plevel stop)
    {
        if( stop < sob->_low_buy_stop ){
            sob->_low_buy_stop = stop;
        }
        if( stop > sob->_high_buy_stop ){
            sob->_high_buy_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(SOB_CLASS *sob, plevel stop)
    {
        sob->_low_buy_stop = stop + 1;
        if( sob->_low_buy_stop > sob->_high_buy_stop ){
            sob->_low_buy_stop = sob->_end;
            sob->_high_buy_stop = sob->_beg - 1;
        }
    }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    static bool
    stop_chain_is_empty(SOB_CLASS *sob, stop_chain_type* c)
    {
        static auto ifcond =
            [](const stop_chain_type::value_type & v){
                return v.is_buy == Redirect;
            };
        auto biter = c->cbegin();
        auto eiter = c->cend();
        auto riter = find_if(biter, eiter, ifcond);
        return (riter == eiter);
    }
};

template<bool Redirect>
struct SOB_CLASS::_stop_exec<false, Redirect>
        : public _stop_exec<true, false> {
    static void
    adjust_state_after_pull(SOB_CLASS *sob, plevel stop)
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
    adjust_state_after_insert(SOB_CLASS *sob, plevel stop)
    {
        if( stop > sob->_high_sell_stop ){
            sob->_high_sell_stop = stop;
        }
        if( stop < sob->_low_sell_stop ){
            sob->_low_sell_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(SOB_CLASS *sob, plevel stop)
    {
        sob->_high_sell_stop = stop - 1;
        if( sob->_high_sell_stop < sob->_low_sell_stop ){
            sob->_high_sell_stop = sob->_beg - 1;
            sob->_low_sell_stop = sob->_end;
        }
    }
};


// base clase
template<typename ChainTy, bool IsBase>
struct SOB_CLASS::_chain_op{
protected:
    template<typename BndlTy>
    static void
    push(SOB_CLASS *sob, plevel p, ChainTy* c, BndlTy&& bndl, bool is_limit){     
        auto id = bndl.id;        
        c->emplace_back(bndl); /* moving bndl */                
        sob->_id_cache.emplace(
             std::piecewise_construct,
             std::forward_as_tuple(id),
             std::forward_as_tuple(--(c->end()), is_limit, p)
         );       
    }
};

// limit chain special
template<>
struct SOB_CLASS::_chain_op<typename SOB_CLASS::limit_chain_type, false>
        : protected SOB_CLASS::_chain_op<typename SOB_CLASS::limit_chain_type, true>{
    typedef _chain_op<limit_chain_type, true> base_type;
    typedef limit_bndl bndl_type;
    
    template<bool IsBuy>
    static void
    push(SOB_CLASS *sob, plevel p, limit_bndl&& bndl){        
        limit_chain_type *c = &p->first;
        base_type::push(sob, p, c, bndl, true);     
        _limit_exec<IsBuy>::adjust_state_after_insert(sob, p, c);
    }

    static limit_bndl
    pop(SOB_CLASS *sob, id_type id)
    {        
        const chain_iter_wrap& iwrap = sob->_from_cache(id);
        assert( iwrap.is_limit );
                
        limit_bndl bndl = *(iwrap.l_iter);
        plevel p = iwrap.p;
        limit_chain_type *c = &(p->first);
                
        c->erase(iwrap.l_iter); // first
        sob->_id_cache.erase(id);  // second
        
        if( bndl && c->empty() ){
            /*
             * we can compare vs bid because if we get here and the order
             * is a buy it must be <= the best bid, otherwise its a sell
             * (remember, p is empty if we get here)
             */
             (p <= sob->_bid)
                 ? _limit_exec<true>::adjust_state_after_pull(sob, p)
                 : _limit_exec<false>::adjust_state_after_pull(sob, p);
        }
        return bndl;
    }
};

// stop chain special
template<>
struct SOB_CLASS::_chain_op<typename SOB_CLASS::stop_chain_type, false> 
        : protected SOB_CLASS::_chain_op<typename SOB_CLASS::stop_chain_type, true>{
    typedef _chain_op<stop_chain_type, true> base_type;
    typedef stop_bndl bndl_type;
 
    static void
    push(SOB_CLASS *sob, plevel p, stop_bndl&& bndl){
        stop_chain_type *c = &p->second;
        bool is_buy = bndl.is_buy;
        
        base_type::push(sob, p, c, bndl, false);
        
        is_buy ? _stop_exec<true>::adjust_state_after_insert(sob, p)
               : _stop_exec<false>::adjust_state_after_insert(sob, p);
    }

    static stop_bndl
    pop(SOB_CLASS *sob, id_type id)
    {  
        const chain_iter_wrap& iwrap = sob->_from_cache(id);
        assert( !iwrap.is_limit );
        
        stop_bndl bndl = *(iwrap.s_iter);
        plevel p = iwrap.p;
        stop_chain_type *c = &(p->second);       
        
        c->erase(iwrap.s_iter); // first
        sob->_id_cache.erase(id); // second 
        
        if( !bndl )
            return bndl;
        
        if( _order::is_buy_stop(bndl) ){
            if( _stop_exec<true>::stop_chain_is_empty(sob, c) ){
                _stop_exec<true>::adjust_state_after_pull(sob, p);
            }
        }else{
            if( _stop_exec<false>::stop_chain_is_empty(sob, c) ){
                _stop_exec<false>::adjust_state_after_pull(sob, p);
            }
        }
        return bndl;
    }
};

}; /* sob */

#undef SOB_CLASS
