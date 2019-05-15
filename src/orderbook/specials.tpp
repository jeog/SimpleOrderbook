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

#include "../../include/simpleorderbook.hpp"

/*
 * INCLUDE directly by any source that needs to utilize the friend struct
 *   specializations declared in SimpleOrderbook::SimpleOrderbookBase 
 */

namespace sob{

namespace detail{

namespace exec{

template<bool BidSide, bool Redirect=BidSide> /* SELL, hit bids */
struct core
        : public sob_types {    
    static constexpr plevel
    begin(const sob_class *sob)
    { return std::max(sob->_bid, sob->_high_buy_aon); }
    
    static constexpr plevel
    end(const sob_class *sob)
    { return std::min(sob->_low_buy_limit, sob->_low_buy_aon); }
    
    static constexpr bool
    inside_of(plevel p, plevel p2)
    { return p >= p2; }
    
    static constexpr plevel
    next(plevel p)
    { return p - 1; }
    
    static constexpr plevel
    next_or_jump(const sob_class *sob, plevel p)
    { return std::min(next(p), begin(sob)); }

    static constexpr bool
    in_window(const sob_class * sob, plevel p)
    { return p <= begin(sob) && p >= end(sob); }
    
    static constexpr bool
    is_tradable(const sob_class *sob, plevel p)
    { return p <= begin(sob); }
    
    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    static bool
    find_new_best_inside(sob_class* sob)
    {
        /* if on an empty chain 'jump' to next that isn't, reset _ask as we go */
        core<Redirect>::_jump_to_nonempty_chain(sob);
        /* reset size; if we run out of orders reset state/cache and return */
        if( !core<Redirect>::_check_and_reset(sob) ){
            return false;
        }
        core<Redirect>::_adjust_limit_pointer(sob);
        return true;
    }
private:
    static inline void
    _jump_to_nonempty_chain(sob_class* sob)
    {
        for( ;
             (sob->_bid >= sob->_beg) && sob->_bid->limit_chain_is_empty();
             --sob->_bid ) // BUG FIX Apr 27 2019 - reverse order of conditions
           {
           }
    }

    static bool
    _check_and_reset(sob_class* sob)
    {
        if(sob->_bid < sob->_beg){
            sob->_low_buy_limit = sob->_end;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(sob_class* sob)
    {
        if( sob->_bid < sob->_low_buy_limit )
            sob->_low_buy_limit = sob->_bid;        
    }
};


template<bool Redirect> /* BUY, hit offers */
struct core<false, Redirect>
        : public core<true, false> {
    friend core<true,false>;
    
    static constexpr plevel
    begin(const sob_class *sob)
    { return std::min(sob->_ask, sob->_low_sell_aon); }
  
    static constexpr plevel
    end(const sob_class *sob)
    { return std::max(sob->_high_sell_limit, sob->_high_sell_aon); }
    
    static constexpr bool
    inside_of(plevel p, plevel p2)
    { return p <= p2; }
    
    static constexpr plevel
    next(plevel p)
    { return p + 1; }
    
    static constexpr plevel
    next_or_jump(const sob_class *sob, plevel p)
    { return std::max(next(p), begin(sob)); }
  
    static constexpr bool
    in_window(const sob_class * sob, plevel p)
    { return p >= begin(sob) && p <= end(sob); }
    
    static constexpr bool
    is_tradable(const sob_class *sob, plevel p)
    { return p >= begin(sob); }
  
private:
    static inline void
    _jump_to_nonempty_chain(sob_class *sob)
    {
        for( ;
             (sob->_ask < sob->_end) && sob->_ask->limit_chain_is_empty();
             ++sob->_ask ) // BUG FIX Apr 27 2019 - reverse order of conditions
            {
            }
    }

    static bool
    _check_and_reset(sob_class *sob)
    {
        if( sob->_ask >= sob->_end ){
            sob->_high_sell_limit = sob->_beg - 1;
            return false;
        }
        return true;
    }

    static inline void
    _adjust_limit_pointer(sob_class *sob)
    {
        if( sob->_ask > sob->_high_sell_limit )
            sob->_high_sell_limit = sob->_ask;        
    }
};


template<bool BidSide>
struct aon
        : public sob_types {
 
     static constexpr bool
     in_window(const sob_class* sob, plevel p)
     { return (p >= sob->_low_buy_aon) && (p <= sob->_high_buy_aon); }
     
     static void
     adjust_state_after_pull(sob_class *sob, plevel p)
     {   
         if( p == sob->_high_buy_aon ){
             for( ;
                  (sob->_high_buy_aon >= sob->_low_buy_aon )
                      && sob->_high_buy_aon->aon_chain_is_empty<true>();
                  --(sob->_high_buy_aon) )
             {
             }
         }else if( p == sob->_low_buy_aon ){
             for( ;
                  ( sob->_low_buy_aon <= sob->_high_sell_aon )                      
                      && sob->_low_buy_aon->aon_chain_is_empty<true>();
                  ++(sob->_low_buy_aon) )
             {
             }
         }         
         if( sob->_high_buy_aon < sob->_low_buy_aon ){
             sob->_high_buy_aon = sob->_beg - 1;
             sob->_low_buy_aon = sob->_end;              
         }
     }

     static void
     adjust_state_after_insert(sob_class *sob, plevel p)
     {
         if( p > sob->_high_buy_aon )
             sob->_high_buy_aon = p;         
         if( p < sob->_low_buy_aon )
             sob->_low_buy_aon = p;
     }
     
     
     static std::vector<std::pair<plevel,std::reference_wrapper<aon_bndl>>>
     overlapping(const sob_class *sob, plevel p)
     {         
         std::vector<std::pair<plevel,std::reference_wrapper<aon_bndl>>> tmp;
         // lowest first          
         for( ; p <= sob->_high_buy_aon; ++p ){
             aon_chain_type *ac = p->get_aon_chain<true>();
             if( ac ){
                 for( auto& elem : *ac )
                     tmp.emplace_back( p, elem );                          
             }                   
         }        
         return tmp;
     }    
     
};

template<>
struct aon<false>
        : public aon<true>{
 
    static constexpr bool
    in_window(const sob_class* sob, plevel p)
    { return (p <= sob->_high_sell_aon) && (p >= sob->_low_sell_aon); }
    
    static void
    adjust_state_after_pull(sob_class *sob, plevel p)
    {   
        if( p == sob->_low_sell_aon ){
            for( ;
                 (sob->_low_sell_aon <= sob->_high_sell_aon )
                     && sob->_low_sell_aon->aon_chain_is_empty<false>();
                 ++(sob->_low_sell_aon) )
            {
            }
        }else if( p == sob->_high_sell_aon ){
            for( ;
                 (sob->_high_sell_aon >= sob->_low_sell_aon )
                     && sob->_high_sell_aon->aon_chain_is_empty<false>();
                 --(sob->_high_sell_aon) )
            {
            }
        }        
        if( sob->_low_sell_aon > sob->_high_sell_aon ){
            sob->_low_sell_aon = sob->_end;
            sob->_high_sell_aon = sob->_beg - 1;
        }
    }

    static void
    adjust_state_after_insert(sob_class *sob, plevel p)
    {
        if( p < sob->_low_sell_aon )
            sob->_low_sell_aon = p;        
        if( p > sob->_high_sell_aon )
            sob->_high_sell_aon = p;
    }
    
    static std::vector<std::pair<plevel,std::reference_wrapper<aon_bndl>>>
    overlapping(const sob_class *sob, plevel p)
    {
        std::vector<std::pair<plevel,std::reference_wrapper<aon_bndl>>> tmp;
        // highest first
        for( ; p >= sob->_low_sell_aon; --p ){
            aon_chain_type *ac = p->get_aon_chain<false>();
            if( ac ){
                for( auto& elem : *ac )
                    tmp.emplace_back( p, elem );                         
            }                    
        }        
        return tmp;
    }
    
};


template<bool BidSide>
struct limit
        : public sob_types {

    static constexpr bool
    in_window(const sob_class *sob, plevel p)
    { return p <= sob->_bid && p >= sob->_low_buy_limit; } 

    static constexpr bool
    is_tradable(const sob_class *sob, plevel p)
    { return p <= sob->_bid; }

    static void
    adjust_state_after_pull(sob_class *sob, plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit >= sob->_low_buy_limit );
        assert( limit <= sob->_bid );
        if( limit == sob->_low_buy_limit ){
            ++sob->_low_buy_limit;
        }
        if( limit == sob->_bid ){
            core<true>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(sob_class *sob, plevel limit)
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
struct limit<false>
        : public limit<true>{
    static constexpr bool
    in_window(const sob_class *sob, plevel p)
    { return p >= sob->_ask && p <= sob->_high_sell_limit; }
    
    static constexpr bool
    is_tradable(const sob_class *sob, plevel p)
    { return p >= sob->_ask; }
    
    static void
    adjust_state_after_pull(sob_class *sob, plevel limit)
    {   /* working on guarantee that this is the *last* order at this level*/
        assert( limit <= sob->_high_sell_limit );
        assert( limit >= sob->_ask );
        if( limit == sob->_high_sell_limit ){
            --sob->_high_sell_limit;
        }
        if( limit == sob->_ask ){
            core<false>::find_new_best_inside(sob);
        }
    }

    static void
    adjust_state_after_insert(sob_class *sob, plevel limit)
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

template<bool BuyStop>
struct stop  
        : public sob_types {
    static void
    adjust_state_after_pull(sob_class *sob, plevel stop)
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
    adjust_state_after_insert(sob_class *sob, plevel stop)
    {
        if( stop < sob->_low_buy_stop ){
            sob->_low_buy_stop = stop;
        }
        if( stop > sob->_high_buy_stop ){
            sob->_high_buy_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(sob_class *sob, plevel stop)
    {
        sob->_low_buy_stop = stop + 1;
        if( sob->_low_buy_stop > sob->_high_buy_stop ){
            sob->_low_buy_stop = sob->_end;
            sob->_high_buy_stop = sob->_beg - 1;
        }
    }
};


template<>
struct stop<false>
        : public stop<true> {
    static void
    adjust_state_after_pull(sob_class *sob, plevel stop)
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
    adjust_state_after_insert(sob_class *sob, plevel stop)
    {
        if( stop > sob->_high_sell_stop ){
            sob->_high_sell_stop = stop;
        }
        if( stop < sob->_low_sell_stop ){
            sob->_low_sell_stop = stop;
        }
    }

    static void
    adjust_state_after_trigger(sob_class *sob, plevel stop)
    {
        sob->_high_sell_stop = stop - 1;
        if( sob->_high_sell_stop < sob->_low_sell_stop ){
            sob->_high_sell_stop = sob->_beg - 1;
            sob->_low_sell_stop = sob->_end;
        }
    }
};

}; /* exec */


// base clase
template<typename ChainTy, bool IsBase=false>
struct chain
        : public sob_types {
    static constexpr bool is_limit = std::is_same<ChainTy, limit_chain_type>::value;
    static constexpr bool is_stop = std::is_same<ChainTy, stop_chain_type>::value;
    static constexpr bool is_aon = std::is_same<ChainTy, aon_chain_type>::value;
    typedef chain<ChainTy, false> derived_type;

protected:
    static size_t
    size(ChainTy *c)
    {
        size_t sz = 0;
        if( c ){
            for( const typename ChainTy::value_type& e : *c )
                sz += e.sz;
        }
        return sz;
    }
    
    template<typename FuncTy>
    static size_t
    size_if(ChainTy *c, FuncTy pred)
    {
        size_t sz = 0;
        if( c ){
            for( const typename ChainTy::value_type& e : *c){
                if( pred(e) )
                    sz += e.sz;
            }
        }
        return sz;
    }   

    template<typename FuncTy>
    static bool
    atleast_if(ChainTy *c, size_t sz, FuncTy pred )
    { 
        size_t tot = 0;
        if( c ){
            for( const typename ChainTy::value_type& e : *c){
                if( pred(e) ){
                    tot += e.sz;
                    if( tot >= sz )
                        return true;
                }
            }
        }
        return false;
    }
    
    template<typename BndlTy>
    static void
    push(sob_class *sob, plevel p, BndlTy&& bndl){      
        ChainTy *c = chain<ChainTy, false>::get(p);
        c->push_back( std::move(bndl) ); 
        /* moved bndl but id is still valid (see bndl.cpp)*/         
        sob->_id_cache.emplace(
             std::piecewise_construct,
             std::forward_as_tuple(bndl.id),
             std::forward_as_tuple(--(c->end()), p)
         );       
    }
    
public:
    static constexpr size_t
    size(plevel p)
    { return size( derived_type::get(p) ); }
        
    template<typename FuncTy>
    static constexpr size_t
    size_if(plevel p, FuncTy pred)
    { return size_if( derived_type::get(p), pred ); }   
    
    template<typename FuncTy>
    static constexpr size_t
    atleast_if(plevel p, size_t sz, FuncTy pred)
    { return atleast_if( derived_type::get(p), sz, pred ); }

    static constexpr sob::order_type
    as_order_type()
    { return sob::order_type::limit; }
};


// limit chain special
template<>
struct chain<typename sob_types::limit_chain_type, false>
        : public chain<typename sob_types::limit_chain_type, true>{
    typedef chain<limit_chain_type, true> base_type;
    typedef limit_chain_type::iterator iter_type;
    typedef limit_bndl bndl_type;

protected:
    template<bool BuyLimit>
    static void
    copy_bndl_to_aon_chain(sob_class *sob, plevel p, iter_type& iter)
    {
        assert( order::is_AON(*iter) );
        /*
         *  all we we do is copy the limit_bndl to back of aon chain
         *  (WE DONT REMOVE IT FROM THE LIMIT CHAIN)
         */        
        auto& iwrap = sob->_from_cache(iter->id);
        p->push_aon_bndl<BuyLimit>( *iter );
        iwrap.a_iter = p->get_aon_chain<BuyLimit>()->end();
        --(iwrap.a_iter);
        iwrap.type = chain_iter_wrap::aon_itype<BuyLimit>::value;
        exec::aon<BuyLimit>::adjust_state_after_insert(sob, p);        
    }
    
public:
    static void
    copy_bndl_to_aon_chain(sob_class *sob, plevel p, iter_type& iter)
    {
        sob->_is_buy_order(p, *iter)
            ? copy_bndl_to_aon_chain<true>(sob, p, iter)
            : copy_bndl_to_aon_chain<false>(sob, p, iter);
    }
    
    template<bool BuyLimit>
    static void
    push(sob_class *sob, plevel p, limit_bndl&& bndl)
    {       
        base_type::push(sob, p, std::move(bndl) );   
        exec::limit<BuyLimit>::adjust_state_after_insert(sob, p);
    }

    static limit_bndl
    pop(sob_class *sob, sob::id_type id)
    {                   
        const chain_iter_wrap& iwrap = sob->_from_cache(id);         
        assert( iwrap.is_limit() );
                
        limit_bndl bndl = *(iwrap.l_iter); // copy
        plevel p = iwrap.p;
        limit_chain_type *c = p->get_limit_chain();
              
        c->erase(iwrap.l_iter); // first        
        sob->_id_cache.erase(id);  // second
                             
        /* if an aon is now at the front we need to move to aon chain */
        while( !c->empty() && order::is_AON( c->front() ) ){       
            auto b = c->begin();
            copy_bndl_to_aon_chain( sob, p, b );                   
            c->erase( b );
        }
    
        if( c->empty() ){               
            /*
             * we can compare vs bid because if we get here and the order
             * is a buy it must be <= the best bid, otherwise its a sell             
             */
            (p <= sob->_bid)
                ? exec::limit<true>::adjust_state_after_pull(sob, p)
                : exec::limit<false>::adjust_state_after_pull(sob, p);
        }       
         
        return bndl;
    }

    static limit_chain_type*
    get(plevel p)
    { return p->get_limit_chain(); }
};


// aon chain special
// TODO inherit from limit special
template<>
struct chain<typename sob_types::aon_chain_type, false>
        : public chain<typename sob_types::aon_chain_type, true>{
    typedef chain<aon_chain_type, true> base_type;
    typedef aon_bndl bndl_type;   

    template<bool BuyLimit>
    static void
    push(sob_class *sob, plevel p, aon_bndl&& bndl )
    {
        p->push_aon_bndl<BuyLimit>( std::move(bndl) );
        auto e = p->get_aon_chain<BuyLimit>()->end();
        sob->_id_cache.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(bndl.id),
            std::forward_as_tuple(--e, p, BuyLimit)
        );        
         exec::aon<BuyLimit>::adjust_state_after_insert(sob, p);
    }
  

    static aon_bndl
    pop(sob_class *sob, sob::id_type id)
    {        
        const chain_iter_wrap& iwrap = sob->_from_cache(id);              
        
        // TODO share this rather than do two lookups
        if( iwrap.is_limit() )            
            return chain<limit_chain_type>::pop(sob, id);
                
        assert( iwrap.is_aon() );                           
        aon_bndl bndl = *(iwrap.a_iter);  
        plevel p = iwrap.p;
        bool is_buy = iwrap.is_aon_buy();
        
        aon_chain_type *c = is_buy ? p->get_aon_chain<true>()
                                   : p->get_aon_chain<false>();        
        c->erase( iwrap.a_iter ); // first                           
        sob->_id_cache.erase(id);  // second
         
        if( c->empty() ){
            if( is_buy ){
                p->destroy_aon_chain<true>(); // first
                exec::aon<true>::adjust_state_after_pull(sob, p); // second
            }else{
                p->destroy_aon_chain<false>();
                exec::aon<false>::adjust_state_after_pull(sob, p);
            }
        }
        return bndl;
    }  

    template<bool BuyChain>
    static constexpr aon_chain_type*
    get(plevel p)
    { return p->get_aon_chain<BuyChain>(); }
    
    template<bool BuyChain>
    static constexpr size_t
    size(plevel p)
    { return base_type::size( get<BuyChain>(p) ); }

    template<side_of_trade Side>
    static constexpr size_t
    size(plevel p)
    { return (Side == side_of_trade::both) ? size<true>(p) + size<false>(p)
            : size<Side == side_of_trade::buy>(p); }

};


// stop chain special
template<>
struct chain<typename sob_types::stop_chain_type, false>
        : public chain<typename sob_types::stop_chain_type, true>{
    typedef chain<stop_chain_type, true> base_type;
    typedef stop_bndl bndl_type;

    static void
    push(sob_class *sob, plevel p, stop_bndl&& bndl)
    {        
        bool is_buy = bndl.is_buy;
        base_type::push(sob, p, std::move(bndl) );        
        is_buy ? exec::stop<true>::adjust_state_after_insert(sob, p)
               : exec::stop<false>::adjust_state_after_insert(sob, p);
    }

    static stop_bndl
    pop(sob_class *sob, sob::id_type id)
    {  
        const chain_iter_wrap& iwrap = sob->_from_cache(id);
        assert( iwrap.is_stop() );
        
        stop_bndl bndl = *(iwrap.s_iter); // copy
        plevel p = iwrap.p;
        stop_chain_type *c = p->get_stop_chain();       
        
        c->erase(iwrap.s_iter); // first
        sob->_id_cache.erase(id); // second 
     
        if( c->empty() ){
            bndl.is_buy ? exec::stop<true>::adjust_state_after_pull(sob, p)
                        : exec::stop<false>::adjust_state_after_pull(sob, p);            
        }

        return bndl;
    }

    static stop_chain_type*
    get(plevel p)
    { return p->get_stop_chain(); }

    /* doesn't differentiate between stop & stop/limit */
    static constexpr sob::order_type
    as_order_type()
    { return sob::order_type::stop; }
};


template<sob::side_of_trade Side = sob::side_of_trade::both>
struct range
        : public sob_types{
    template<typename ChainTy>
    static std::pair<plevel, plevel>
    get(const sob_class* sob, size_t depth ) {
        static_assert(chain<ChainTy>::is_limit, "currently only for limits");
        auto p = _helper<ChainTy>::get(sob);
        return {std::max(sob->_bid - depth + 1, p.first),
                std::min(sob->_ask + depth - 1, p.second)};
    }

    template<typename ChainTy>
    static std::pair<plevel, plevel>
    get(const sob_class* sob )
    { return _helper<ChainTy>::get(sob); }

    template<typename ChainTy1, typename ChainTy2, side_of_trade S=Side>
    static inline std::pair<plevel, plevel>
    get(const sob_class *sob)
    {
        auto p1 = range<S>::template get<ChainTy1>(sob);
        auto p2 = range<S>::template get<ChainTy2>(sob);
        return {std::min(p1.first, p2.first), std::max(p1.second, p2.second)};
    }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {std::min(sob->_low_buy_limit, sob->_ask),
                  std::max(sob->_high_sell_limit, sob->_bid)}; }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {std::min(sob->_low_sell_stop, sob->_low_buy_stop),
                  std::max(sob->_high_sell_stop, sob->_high_buy_stop)}; }
    };

    // just aon orders in aon_chain
    template<typename Dummy>
    struct _helper<aon_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {std::min(sob->_low_buy_aon, sob->_low_sell_aon),
                  std::max(sob->_high_sell_aon, sob->_high_buy_aon)};  }
    };
};


template<>
struct range<sob::side_of_trade::buy>
        : public range<sob::side_of_trade::both> {
    using base_type = range<sob::side_of_trade::both>;

    template<typename ChainTy>
    static std::pair<plevel, plevel>
    get(const sob_class* sob, size_t depth) {
        static_assert(chain<ChainTy>::is_limit, "currently only for limits");
        auto p =_helper<ChainTy>::get(sob);
        return {std::max(sob->_bid - depth +1, p.first), sob->_bid};
    }

    template<typename ChainTy>
    static inline std::pair<plevel, plevel>
    get(const sob_class* sob)
    { return _helper<ChainTy>::get(sob); }

    template<typename ChainTy1, typename ChainTy2>
    static inline std::pair<plevel, plevel>
    get(const sob_class *sob)
    { return base_type::get<ChainTy1, ChainTy2, side_of_trade::buy>(sob); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_low_buy_limit, sob->_bid}; }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_low_buy_stop, sob->_high_buy_stop}; }
    };

    // just aon orders in aon chain
    template<typename Dummy>
    struct _helper<aon_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_low_buy_aon, sob->_high_buy_aon}; }
    };
};


template<>
struct range<sob::side_of_trade::sell>
        : public range<sob::side_of_trade::both> {
    using base_type = range<sob::side_of_trade::both>;

    template<typename ChainTy>
    static std::pair<plevel, plevel>
    get(const sob_class* sob, size_t depth) {
        static_assert(chain<ChainTy>::is_limit, "currently only for limits");
        auto p = _helper<ChainTy>::get(sob);
        return {sob->_ask, std::min(sob->_ask + depth - 1, p.second)};
    }

    template<typename ChainTy>
    static inline std::pair<plevel, plevel>
    get(const sob_class* sob)
    { return _helper<ChainTy>::get(sob); }

    template<typename ChainTy1, typename ChainTy2>
    static inline std::pair<plevel, plevel>
    get(const sob_class *sob)
    { return base_type::get<ChainTy1, ChainTy2, side_of_trade::sell>(sob); }

private:
    template<typename DummyChainTY, typename Dummy=void>
    struct _helper{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_ask, sob->_high_sell_limit}; }
    };

    template<typename Dummy>
    struct _helper<stop_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_low_sell_stop, sob->_high_sell_stop}; }
    };

    // just aon orders in aon chain
    template<typename Dummy>
    struct _helper<aon_chain_type, Dummy>{
        static inline std::pair<plevel, plevel>
        get(const sob_class* sob)
        { return {sob->_low_sell_aon, sob->_high_sell_aon}; }
    };
};


template<sob::side_of_market Side>
struct depth  : public sob_types {
    typedef size_t mapped_type;

    static constexpr side_of_trade SIDE_OF_TRADE =
        (Side == side_of_market::bid) ? side_of_trade::buy : side_of_trade::sell;

    static inline size_t
    build_value(const sob_class* sob, plevel p, size_t d)
    { return d; }
};

template<>
struct depth<sob::side_of_market::both>  : public sob_types {
    typedef std::pair<size_t, sob::side_of_market> mapped_type;

    static constexpr side_of_trade SIDE_OF_TRADE = side_of_trade::both;

    static inline mapped_type
    build_value(const sob_class* sob, plevel p, size_t d)
    {
        auto s = (p >= sob->_ask) ? sob::side_of_market::ask
                                  : sob::side_of_market::bid;
        return std::make_pair(d,s);
    }
};


template<>
struct promise_helper< std::pair<id_type, sob_types::callback_queue_type> >
        : public sob_types {    
    template<typename A>    
    static constexpr std::pair<id_type, A> 
    build_value(id_type ret, const A& a) { return {ret, a}; }
    
    static constexpr order_exec_cb_bndl::type 
    callback_type = order_exec_cb_bndl::type::synchronous;
    
    static constexpr bool is_synchronous = true;
};

template<>
struct promise_helper<id_type>
        : public sob_types {
    template<typename A>     
    static constexpr id_type
    build_value(id_type ret, const A& a) { return ret; }
    
    static constexpr order_exec_cb_bndl::type 
    callback_type = order_exec_cb_bndl::type::asynchronous;
    
    static constexpr bool is_synchronous = false;
};

}; /* detail */

}; /* sob */
