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

#include <iterator>
#include <iomanip>
#include "../include/simpleorderbook.hpp"

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbookImpl(TickPrice<TickRatio> min, size_t incr)
    :  
        _bid_size(0),
        _ask_size(0),
        _last_size(0),    
        /* lowest price */
        _base(min),
        /* actual orderbook object */
        _book(incr + 1), /*pad the beg side */
        /***************************************************************
                      *** our ersatz iterator approach ****
                                i = [ 0, incr )
     
         vector iter    [begin()]                               [ end() ]
         internal pntr  [ _base ][ _beg ]           [ _end - 1 ][ _end  ]
         internal index [ NULL  ][   i  ][ i+1 ]...   [ incr-1 ][  NULL ]
         external price [ THROW ][ min  ]              [  max  ][ THROW ]           
        *****************************************************************/
        _beg( &(*_book.begin()) + 1 ), 
        _end( &(*_book.end())), 
        _last( 0 ),  
        _bid( _beg - 1),
        _ask( _end ),
        /* cache range vals for faster lookups */
        _low_buy_limit( _end ),
        _high_sell_limit( _beg - 1 ),
        _low_buy_stop( _end ),
        _high_buy_stop( _beg - 1 ),
        _low_sell_stop( _end ),
        _high_sell_stop( _beg - 1 ),
        /* internal trade stats */
        _total_volume(0),
        _last_id(0), 
        _timesales(),                  
        /* trade callbacks */
        _deferred_callbacks(), 
        _busy_with_callbacks(false),
        /* our threaded approach to order queuing/exec */
        _order_queue(),
        _order_queue_mtx(),    
        _order_queue_cond(),
        _noutstanding_orders(0),                       
        _need_check_for_stops(false),
        /* core sync objects */
        _master_mtx(),  
        _master_run_flag(true)       
    {                               
        /*** DONT THROW AFTER THIS POINT ***/
        _order_dispatcher_thread = 
            std::thread(std::bind(&SOB_CLASS::_threaded_order_dispatcher,this));               
    }


SOB_TEMPLATE 
SOB_CLASS::~SimpleOrderbookImpl()
    { 
        _master_run_flag = false;       
        try{ 
            {
                std::lock_guard<std::mutex> lock(_order_queue_mtx);
                _order_queue.push(order_queue_elem()); 
                /* don't incr _noutstanding_orders; we break main loop before we can decr */
            }    
            _order_queue_cond.notify_one();
            if( _order_dispatcher_thread.joinable() ){
                _order_dispatcher_thread.join();
            }
        }catch(...){
        }        
    }

/* 
 * _high_low::get(sob, h, l, depth) : populate plevels using passed depth
 *     from 'inside' bid/ask and internal bounds
 *     
 * _high_low::get(sob, h,, l) : populate plevels using cached internal pointers
 *
 */
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


/*
 * _order: utilities for individual orders
 */
SOB_TEMPLATE
struct SOB_CLASS::_order {
    template<typename ChainTy>
    static typename ChainTy::mapped_type&
    find( plevel p, id_type id)
    {
        ChainTy *c = _chain<ChainTy>::get(p);
        if( c ){
            try{
                return c->at(id);
            }catch(std::out_of_range&){}
        }
        return ChainTy::mapped_type::null;
    }

    inline static _order_bndl&
    as_base_bndl(plevel p, id_type id, bool is_limit)
    { return is_limit ? dynamic_cast<_order_bndl&>(find<limit_chain_type>(p, id))
                      : dynamic_cast<_order_bndl&>(find<stop_chain_type>(p, id)); }

    template<typename ChainTy, typename Dummy=void>
    struct _info{
        static inline order_info
        generate()
        { return {order_type::null,false,0,0,0}; }
    };

    template<typename Dummy>
    struct _info<limit_chain_type, Dummy> {
        static inline order_info
        generate(const SimpleOrderbookImpl *sob,
                 id_type id,
                 typename SOB_CLASS::plevel p,
                 typename SOB_CLASS::limit_chain_type* c)
        { return {order_type::limit, (p < sob->_ask), sob->_itop(p), 0, c->at(id).sz}; }
    };

    template<typename Dummy>
    struct _info<stop_chain_type, Dummy> {
        static order_info
        generate(const SimpleOrderbookImpl *sob,
                 id_type id,
                 typename SOB_CLASS::plevel p,
                 typename SOB_CLASS::stop_chain_type* c)
        {
            auto& bndl = c->at(id);
            if( bndl.limit != 0 ){
                return {order_type::stop_limit, bndl.is_buy, bndl.limit,
                        sob->_itop(p), bndl.sz};
            }
            return {order_type::stop, bndl.is_buy, 0, sob->_itop(p), bndl.sz};
        }
    };

    template<typename ChainTy,typename Dummy=void>
    struct _dump {};

    template<typename Dummy>
    struct _dump<limit_chain_type, Dummy> {
        static inline void
        dump( std::ostream& out,
              const std::pair<id_type, SOB_CLASS::limit_bndl>& p )
        { out << " <" << p.second.sz << " #" << p.first << "> "; }
    };

    template<typename Dummy>
    struct _dump<stop_chain_type, Dummy> {
        static inline void
        dump( std::ostream& out,
              const std::pair< id_type, SOB_CLASS::stop_bndl>& p )
        {
            double limit = p.second.limit;
            out << " <" << (p.second.is_buy ? "B " : "S ")
                        << p.second.sz << " @ "
                        << (limit ? std::to_string(limit) : "MKT")
                        << " #" << p.first << "> ";
        }
    };

    /* return an order_info struct for that order id */
    template<typename ChainTy>
    static order_info
    info(const SimpleOrderbookImpl *sob,id_type id)
    {
        plevel p = _chain<ChainTy>::find(sob,id);
        if( p ){
            return info<ChainTy>(sob, id, p);
        }
        return _info<void>::generate();
    }

    /* return an order_info struct for id ONLY at plevel p (FAST) */
    template<typename ChainTy>
    static order_info
    info(const SimpleOrderbookImpl *sob, id_type id, plevel p)
    {
        ChainTy *c = _chain<ChainTy>::get(p);
        if( c ){
            try{
                return _info<ChainTy>::generate(sob, id, p, c);
            }catch(std::out_of_range&){}
        }
        return _info<void>::generate();
    }

    template<typename PrimaryChainTy, typename SecondaryChainTy>
    inline static order_info
    info(const SimpleOrderbookImpl *sob, id_type id, plevel p)
    {
        auto oi =  info<PrimaryChainTy>(sob, id, p);
        if( !oi ){
            oi = info<SecondaryChainTy>(sob, id, p);
        }
        return oi;
    }

    template<typename PrimaryChainTy, typename SecondaryChainTy>
    inline static order_info
    info(const SimpleOrderbookImpl *sob, id_type id, double price)
    { return info<PrimaryChainTy, SecondaryChainTy>(sob, id, sob->_ptoi(price)); }

    template<typename PrimaryChainTy, typename SecondaryChainTy>
    inline static order_info
    info(const SimpleOrderbookImpl *sob, id_type id)
    {
        auto oi =  info<PrimaryChainTy>(sob, id);
        if( !oi ){
            oi = info<SecondaryChainTy>(sob, id);
        }
        return oi;
    }
};


/*
 * _chain::get : get appropriate chain from plevel
 * _chain::size : get size of chain
 * _chain::find : find chain containing a particular order id
 * 
 * (note: the _chain specials inherit from non-special to access base find
 */
SOB_TEMPLATE
template<typename ChainTy, typename Dummy> 
struct SOB_CLASS::_chain {    
protected:
    template<typename InnerChainTy, typename Impl>
    static typename SOB_CLASS::plevel 
    find(const Impl* sob, id_type id)
    {         
        InnerChainTy* c;
        plevel beg;
        plevel end;
        _high_low<>::template get<InnerChainTy>(sob,&end,&beg);
        for( ; beg <= end; ++beg ){
            c = _chain<InnerChainTy>::get(beg);
            for( auto & e : *c) {
                if( e.first == id ){ 
                    return beg;
                }
            }
        }       
        return nullptr;
    }
    
    template<typename InnerChainTy>
    static size_t 
    size(InnerChainTy *c)
    { 
        size_t sz = 0;
        for( const typename InnerChainTy::value_type& e : *c ){
            sz += e.second.sz;
        }
        return sz;
    }  
};


SOB_TEMPLATE
template<typename Dummy> 
struct SOB_CLASS::_chain<typename SOB_CLASS::limit_chain_type, Dummy>
        : public _chain<void> { 
    typedef typename SOB_CLASS::limit_chain_type chain_type;

    static inline chain_type* 
    get(typename SOB_CLASS::plevel p)
    { return &(p->first); } 

    static size_t 
    size(chain_type* c)
    { return _chain<void>::template size(c); }  
  
    template<typename Impl>
    static inline typename SOB_CLASS::plevel    
    find(const Impl* sob, id_type id)
    { return _chain<void>::template find<chain_type>(sob,id); }

    static inline order_type
    as_order_type()
    { return order_type::limit; }
};


SOB_TEMPLATE
template<typename Dummy> 
struct SOB_CLASS::_chain<typename SOB_CLASS::stop_chain_type, Dummy>
        : public _chain<void> { 
    typedef typename SOB_CLASS::stop_chain_type chain_type;
    
    static inline chain_type* 
    get(typename SOB_CLASS::plevel p)
    { return &(p->second); }

    static inline size_t
    size(chain_type* c)
    { return _chain<void>::template size(c); }

    template<typename Impl>
    static inline typename SOB_CLASS::plevel
    find(const Impl* sob, id_type id)
    { return _chain<void>::template find<chain_type>(sob,id); }

    static inline order_type
    as_order_type()
    { return order_type::stop; }
};



/*
 *  _core_exec<bool> : specialization for buy/sell branch in _trade
 *
 *  _limit_exec<bool>  : specialization for buy/sell branch in _pull/_insert
 *
 *  _stop_exec<bool>  : specialization for buy/sell branch in _pull/_insert/_trigger
 *             
 */

SOB_TEMPLATE
template<bool BidSide, bool Redirect> /* SELL, hit bids */
struct SOB_CLASS::_core_exec {
    template<typename Impl>
    static inline bool
    is_executable_chain(const Impl* sob, typename SOB_CLASS::plevel p)
    { return (p <= sob->_bid || !p) && (sob->_bid >= sob->_beg); }

    template<typename Impl>
    static inline typename SOB_CLASS::plevel
    get_inside(const Impl* sob)
    { return sob->_bid; }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    template<typename Impl>
    static bool
    find_new_best_inside(Impl* sob)
    {
        /* if on an empty chain 'jump' to next that isn't, reset _ask as we go */ 
        _core_exec<Redirect>::_jump_to_nonempty_chain(sob);             
        /* reset size; if we run out of orders reset state/cache and return */        
        if( !_core_exec<Redirect>::_check_and_reset_size(sob) ){
            return false;
        }
        _core_exec<Redirect>::_adjust_limit_cache(sob);
        return true;
    }

private:
    template<typename Impl>
    static inline void
    _jump_to_nonempty_chain(Impl* sob)
    {
        for( ; 
             sob->_bid->first.empty() && (sob->_bid >= sob->_beg); 
             --sob->_bid )
           {  
           } 
    }

    template<typename Impl>
    static bool
    _check_and_reset_size(Impl* sob)
    {
        if(sob->_bid < sob->_beg){
            sob->_bid_size = 0;             
            sob->_low_buy_limit = sob->_end; 
            return false;
        }        
        sob->_bid_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                         ::size(&sob->_bid->first);             
        return true;
    }

    template<typename Impl>
    static inline void
    _adjust_limit_cache(Impl* sob)
    {       
        if( sob->_bid < sob->_low_buy_limit ){
            sob->_low_buy_limit = sob->_bid;
        }
    }

};


SOB_TEMPLATE
template<bool Redirect> /* BUY, hit offers */
struct SOB_CLASS::_core_exec<false,Redirect>
        : public _core_exec<true,false> {  
    friend _core_exec<true,false>;

    template<typename Impl>
    static inline bool
    is_executable_chain(const Impl* sob, typename SOB_CLASS::plevel p)
    { return (p >= sob->_ask || !p) && (sob->_ask < sob->_end); }

    template<typename Impl>
    static inline typename SOB_CLASS::plevel
    get_inside(const Impl* sob)
    { return sob->_ask; }

private:
    template<typename Impl>
    static inline void
    _jump_to_nonempty_chain(Impl* sob)
    {
        for( ; 
             sob->_ask->first.empty() && (sob->_ask < sob->_end); 
             ++sob->_ask ) 
            { 
            }  
    }

    template<typename Impl>
    static bool
    _check_and_reset_size(Impl* sob)
    {
        if( sob->_ask >= sob->_end ){
            sob->_ask_size = 0;          
            sob->_high_sell_limit = sob->_beg - 1;    
            return false;
        }
        sob->_ask_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                         ::size(&sob->_ask->first);                     
        return true;
    }

    template<typename Impl>
    static inline void
    _adjust_limit_cache(Impl* sob)
    {       
        if( sob->_ask > sob->_high_sell_limit ){
            sob->_high_sell_limit = sob->_ask;
        }
    }
};


SOB_TEMPLATE
template<bool BuyLimit, typename Dummy> 
struct SOB_CLASS::_limit_exec {
    template<typename Impl>
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

    template<typename Impl>
    static void
    adjust_state_after_insert(Impl *sob,
                              SOB_CLASS::plevel limit, 
                              SOB_CLASS::limit_chain_type* orders) 
    {
        assert( limit >= sob->_beg );
        assert( limit < sob->_end );
        if( limit >= sob->_bid ){
            sob->_bid = limit;
            sob->_bid_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                             ::size(orders);
        }

        if( limit < sob->_low_buy_limit ){
            sob->_low_buy_limit = limit;  
        }
    }

    template<typename Impl>
    static inline bool /* slingshot back to protected fillable */
    fillable(Impl *sob, SOB_CLASS::plevel p, size_t sz, bool is_buy)
    { return is_buy ? _limit_exec<true>::fillable(sob, p, sz)
                    : _limit_exec<false>::fillable(sob, p, sz); }

    template<typename Impl>
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
                tot += e.second.sz;
                if( tot >= sz ){
                    return true;
                }
            }
        }
        return false;
    }
};


SOB_TEMPLATE 
template<typename Dummy>
struct SOB_CLASS::_limit_exec<false, Dummy>
        : public _limit_exec<true, Dummy>{
    template<typename Impl>
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

    template<typename Impl>
    static void
    adjust_state_after_insert(Impl *sob,
                              SOB_CLASS::plevel limit, 
                              SOB_CLASS::limit_chain_type* orders) 
    {
        assert( limit >= sob->_beg );
        assert( limit < sob->_end );
        if( limit <= sob->_ask) {
            sob->_ask = limit;
            sob->_ask_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                             ::size(orders);
        }
        if( limit > sob->_high_sell_limit ){
            sob->_high_sell_limit = limit; 
        }
    }

    template<typename Impl>
    static inline bool
    fillable(Impl *sob, SOB_CLASS::plevel p, size_t sz)
    { return (sob->_bid >= sob->_beg)
              && _limit_exec<true, Dummy>::fillable(p, sob->_bid, sz); }
};


// TODO mechanism to jump to new stop cache vals ??
SOB_TEMPLATE
template<bool BuyStop, bool Redirect /* = BuyStop */> 
struct SOB_CLASS::_stop_exec {
    template<typename Impl>
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

    template<typename Impl>
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

    template<typename Impl>
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
    template<typename Impl>
    static bool
    stop_chain_is_empty(Impl* sob, SOB_CLASS::stop_chain_type* c)
    {
        static auto ifcond = 
            [](const typename SOB_CLASS::stop_chain_type::value_type & v) 
            { 
                return v.second.is_buy == Redirect; 
            };
        auto biter = c->cbegin();
        auto eiter = c->cend();
        auto riter = find_if(biter, eiter, ifcond);
        return (riter == eiter);
    }
};


SOB_TEMPLATE
template<bool Redirect /* = false */> 
struct SOB_CLASS::_stop_exec<false,Redirect> 
        : public _stop_exec<true,false> {
    template<typename Impl>
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

    template<typename Impl>
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

    template<typename Impl>
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


/*
 *  _trade<bool> : the guts of order execution:
 *      match orders against the order book,
 *      adjust internal state,
 *      check for overflows  
 */
SOB_TEMPLATE 
template<bool BidSide>
size_t 
SOB_CLASS::_trade( plevel plev, 
                   id_type id, 
                   size_t size,
                   order_exec_cb_type& exec_cb )
{
    while(size){
        /* can we trade at this price level? */
        if( !_core_exec<BidSide>::is_executable_chain(this, plev) ){
            break;   
        }
        /* trade at this price level */
        size = _hit_chain( _core_exec<BidSide>::get_inside(this), 
                           id, size, exec_cb );                       
        /* reset the inside price level (if we can) OR stop */  
        if( !_core_exec<BidSide>::find_new_best_inside(this) ){
            break;
        }
    }
    return size; /* what we couldn't fill */
}


/*
 * _hit_chain : handles all the trades at a particular plevel
 *              returns what it couldn't fill  
 */
SOB_TEMPLATE
size_t
SOB_CLASS::_hit_chain( plevel plev,
                       id_type id,
                       size_t size,
                       order_exec_cb_type& exec_cb )
{
    size_t amount;
    long long rmndr; 
    auto del_iter = plev->first.begin();

    /* check each order, FIFO, for this plevel */
    for( auto& elem : plev->first ){        
        amount = std::min(size, elem.second.sz);
        /* push callbacks onto queue; update state */
        _trade_has_occured( plev, amount, id, elem.first, exec_cb, 
                            elem.second.exec_cb );

        /* reduce the amount left to trade */ 
        size -= amount;    
        rmndr = elem.second.sz - amount;

        /* deal with advanced order conditions */
        _handle_advanced_order(elem.second, elem.first, rmndr);

        if( rmndr > 0 ){ 
            elem.second.sz = rmndr; /* adjust outstanding order size */
        }else{                    
            ++del_iter; /* indicate removal if we cleared bid */
        }     
        if( size <= 0 ){ 
            break; /* if we have nothing left to trade*/
        }
    }
    plev->first.erase(plev->first.begin(),del_iter);
    return size;
}


SOB_TEMPLATE
void
SOB_CLASS::_trade_has_occured( plevel plev,
                               size_t size,
                               id_type idbuy,
                               id_type idsell,
                               order_exec_cb_type& cbbuy,
                               order_exec_cb_type& cbsell )
{
    /* CAREFUL: we can't insert orders from here since we have yet to finish
       processing the initial order (possible infinite loop); */
    double p = _itop(plev);

    /* buy and sell sides */
    _push_exec_callback(callback_msg::fill, cbbuy, idbuy, idbuy, p, size);
    _push_exec_callback(callback_msg::fill, cbsell, idsell, idsell, p, size);

    _timesales.push_back( std::make_tuple(clock_type::now(), p, size) );
    _last = plev;
    _total_volume += size;
    _last_size = size;
    _need_check_for_stops = true;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_advanced_order(_order_bndl& bndl, id_type id, size_t rmndr)
{
    if( bndl.trigger == condition_trigger::none ){
        assert(bndl.cond == order_condition::none);
        return;
    }
    assert(bndl.cond != order_condition::none);

    if( bndl.trigger == condition_trigger::fill_full && rmndr > 0 ){
        return;
    };

    switch(bndl.cond){
    case order_condition::one_cancels_other:
        _handle_OCO(bndl, id);
        break;
    case order_condition::one_triggers_other:
         _handle_OTO(bndl, id);
        break;
    default:
        throw std::runtime_error("invalid order condition");
    }

    /* SHOULD ONLY GET HERE IF HANDLED ADVANCED ORDER */
    bndl.cond = order_condition::none;
    bndl.trigger = condition_trigger::none;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OCO(_order_bndl& bndl, id_type id)
{
    order_location *loc = bndl.linked_order;
    assert(loc);
    id_type id_old = loc->primary ? loc->id : id;
    /* if we need to remove primary order, indicate new order # */
    _push_OCO_callback( bndl.exec_cb, id_old, id, 0, 0);
    _pull_order(loc->id, loc->price, false, loc->limit_chain);
    delete loc;
    bndl.linked_order = nullptr;
}


SOB_TEMPLATE
void
SOB_CLASS::_handle_OTO(_order_bndl& bndl, id_type id)
{
    id_type id_new = _generate_id();
    _push_OTO_callback(bndl.exec_cb, id, id_new, 0, 0);
    OrderParamaters *params = bndl.contingent_order;
    assert(params);
    _push_order_no_wait( params->get_order_type(), params->is_buy(),
                         params->limit(), params->stop(),
                         params->size(), bndl.exec_cb,
                         order_condition::none, condition_trigger::none,
                         nullptr, nullptr, id_new );
    delete params;
    bndl.contingent_order = nullptr;
}


SOB_TEMPLATE
void 
SOB_CLASS::_threaded_order_dispatcher()
{                 
    for( ; ; ){
        order_queue_elem e;
        {
            std::unique_lock<std::mutex> lock(_order_queue_mtx);      
            _order_queue_cond.wait( 
                lock, 
                [this]{ return !this->_order_queue.empty(); }
            );

            e = std::move(_order_queue.front());
            _order_queue.pop(); 
            if( !_master_run_flag ){
                if( _noutstanding_orders != 0 ){
                    throw std::runtime_error("_noutstanding_orders != 0");
                }
                break;                           
            }
        }         
        
        std::promise<id_type> p = std::move( e.promise );        
        id_type id = e.id; // copy
        if( !id ){ 
            id = _generate_id();
        }
        
        try{
            std::lock_guard<std::mutex> lock(_master_mtx); 
            /* --- CRITICAL SECTION --- */
            _insert_order(e, id);
            /* --- CRITICAL SECTION --- */
        }catch(...){        
            --_noutstanding_orders;
            p.set_exception( std::current_exception() );
            continue;
        }
     
        --_noutstanding_orders;
        p.set_value(id);    
    }    
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_order(order_queue_elem& e, id_type& id)
{
    assert(id > 0);
    if( e.cond == order_condition::none ){
        _route_basic_order(e, id);
        return;
    }
    _route_advanced_order(e, id);
}


SOB_TEMPLATE
fill_type
SOB_CLASS::_route_basic_order(order_queue_elem& e, id_type& id)
{
    fill_type fill = fill_type::none;
    try{    
        /* note the _ptoi() conversions HOLDING THE LOCK */            
        switch( e.type ){            
        case order_type::limit:  
            fill =  e.is_buy
                ? _insert_limit_order<true>(_ptoi(e.limit), e.sz, e.exec_cb, id)
                : _insert_limit_order<false>(_ptoi(e.limit), e.sz, e.exec_cb, id);
            break;
       
        case order_type::market:                
            e.is_buy ? _insert_market_order<true>(e.sz, e.exec_cb, id)
                     : _insert_market_order<false>(e.sz, e.exec_cb, id);
            fill = fill_type::immediate_full;
            break;
      
        case order_type::stop:        
            e.is_buy ? _insert_stop_order<true>(_ptoi(e.stop), e.sz, e.exec_cb, id)
                     : _insert_stop_order<false>(_ptoi(e.stop), e.sz, e.exec_cb, id);
            break;
         
        case order_type::stop_limit:        
            e.is_buy ? _insert_stop_order<true>(_ptoi(e.stop), e.limit, e.sz, e.exec_cb, id)
                     : _insert_stop_order<false>(_ptoi(e.stop), e.limit, e.sz, e.exec_cb, id);
            break;
         
        case order_type::null: 
            /* not the cleanest but most effective/thread-safe 
               e.is_buy indicates to check limits first (not buy/sell)
               success/fail is returned in the in e.id*/
            if( e.limit && is_valid_price(e.limit) ){ // if hint
                id = (id_type)_pull_order(id, e.limit, true, e.is_buy);
                if( id ){
                    break;
                }
            }
            id = (id_type)_pull_order(id, true, e.is_buy);
            break;
        
        default: 
            throw std::runtime_error("invalid order type in order_queue");
        }
        
        execute_admin_callback(e.admin_cb, id);
        if( e.type == order_type::limit || e.type == order_type::market){
            _look_for_triggered_stops(false); /* throw */
        }
        _assert_internal_pointers();
        
    }catch(...){                
        _look_for_triggered_stops(true); /* no throw */
        throw;
    }           
    return fill;
}


SOB_TEMPLATE
void
SOB_CLASS::_route_advanced_order(order_queue_elem& e, id_type& id)
{
    switch(e.cond){
    case order_condition::one_cancels_other:
        _insert_OCO_order(e, id);
        break;
    case order_condition::one_triggers_other:
        _insert_OTO_order(e, id);
        break;
    case order_condition::fill_or_kill:
        _insert_FOK_order(e,id);
        break;
    default:
        throw std::runtime_error("invalid advanced order condition");
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::_inject_order( order_queue_elem& e, id_type id, bool partial_ok )
{
    fill_type f = _route_basic_order(e, id);
    return ( f == fill_type::immediate_full
             || (partial_ok && f == fill_type::immediate_partial) );
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OCO_order(order_queue_elem& e, id_type id)
{
    assert(e.cond == order_condition::one_cancels_other);
    assert(e.type != order_type::market);
    assert(e.type != order_type::null);

    OrderParamaters *op = e.cond_order_params.get();
    assert(op);
    assert(op->get_order_type() != order_type::market);
    assert(op->get_order_type() != order_type::null);

    bool partial_ok = (e.cond_trigger == condition_trigger::fill_partial);

    /* if we fill immediately, no need to enter 2nd order */
    if( _inject_order(e, id, partial_ok) ){
        _push_OCO_callback(e.exec_cb, id, id, 0, 0);
        return;
    }

    bool primary_is_limit = (e.type == order_type::limit);
    bool secondary_is_limit = (op->get_order_type() == order_type::limit);

    id_type id2 = _generate_id();
    order_queue_elem e2 = {
        op->get_order_type(), op->is_buy(), op->limit(), op->stop(),
        op->size(), e.exec_cb, e.cond, e.cond_trigger,
        nullptr, e.admin_cb, id2,
        std::move(std::promise<id_type>())
    };

    double p1 = primary_is_limit ? e.limit : e.stop;

    /* if we fill second order immediately, remove first */
    if( _inject_order(e2, id2, partial_ok) ){
        _push_OCO_callback(e.exec_cb, id, id2, 0, 0);
        _pull_order(id, p1, false, primary_is_limit);
        return;
    }

    auto& o1 = _order::as_base_bndl(_ptoi(p1), id, primary_is_limit);
    assert(o1);

    double p2 = secondary_is_limit ? op->limit() : op->stop();
    auto& o2 = _order::as_base_bndl(_ptoi(p2), id2, primary_is_limit);
    assert(o2);

    o1.linked_order = new order_location{p2, id2, secondary_is_limit, false};
    o2.linked_order = new order_location{p1, id, primary_is_limit, true};

    o1.cond = o2.cond = order_condition::one_cancels_other;
    o1.trigger = o2.trigger = e.cond_trigger;
}


SOB_TEMPLATE
void
SOB_CLASS::_insert_OTO_order(order_queue_elem& e, id_type id)
{
    assert(e.cond == order_condition::one_triggers_other);

    OrderParamaters *op = e.cond_order_params.get();
    assert(op);

    bool partial_ok = (e.cond_trigger == condition_trigger::fill_partial);

    if( _inject_order(e, id, partial_ok) ){
        id_type id2 = _generate_id();
        _push_OTO_callback(e.exec_cb, id, id2, 0, 0);
        _push_order_no_wait( op->get_order_type(), op->is_buy(),
                             op->limit(), op->stop(), op->size(),
                             e.exec_cb, order_condition::none,
                             condition_trigger::none, nullptr,
                             nullptr, id2);
        return;
    }

    bool is_limit = (e.type == order_type::limit);
    double price = is_limit ? e.limit : e.stop;
    auto& o = _order::as_base_bndl( _ptoi(price), id, is_limit);
    assert(o);

    o.contingent_order = new OrderParamaters(*op);
    o.cond = order_condition::one_triggers_other;
    o.trigger = e.cond_trigger;
}

SOB_TEMPLATE
void
SOB_CLASS::_insert_FOK_order(order_queue_elem& e, id_type id)
{
    assert(e.type == order_type::limit);
    plevel p = _ptoi(e.limit);
    assert(p);
    size_t sz = (e.cond_trigger == condition_trigger::fill_partial)
              ? 0
              : e.sz;
    /*
     * a bit of trickery here; if all we need is partial fill we check
     * if size of 0 is fillable; if p is <= _bid or >= _ask (and they are
     * valid) we know there's at least 1 order available to trade against
     */
    if( !_limit_exec<>::fillable(this, p, sz, e.is_buy) ){
        _push_exec_callback(callback_msg::kill, e.exec_cb, id, id,
                e.limit, e.sz);
        return;
    }
    _route_basic_order(e, id);
}


SOB_TEMPLATE
template<bool BuyLimit>
sob::fill_type
SOB_CLASS::_insert_limit_order( plevel limit,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                id_type id )
{
    fill_type fill = fill_type::none;
    size_t rmndr = size;
    if( (BuyLimit && limit >= _ask) || (!BuyLimit && limit <= _bid) ){
        /* If there are matching orders on the other side fill @ market
               - pass ref to callback functor, we'll copy later if necessary
               - return what we couldn't fill @ market */
        rmndr = _trade<!BuyLimit>(limit, id, size, exec_cb);
    }

    if( rmndr > 0) {
        /* insert what remains as limit order
           copy callback functor, needs to persist */
        limit_chain_type *orders = &limit->first;
        orders->emplace( id, limit_bndl(rmndr, exec_cb) );
        _limit_exec<BuyLimit>::adjust_state_after_insert(this, limit, orders);
        if( rmndr < size ){
            fill = fill_type::immediate_partial;
        }
    }else{
        fill = fill_type::immediate_full;
    }

    return fill;
}


SOB_TEMPLATE
template<bool BuyMarket>
void
SOB_CLASS::_insert_market_order( size_t size,
                                 order_exec_cb_type exec_cb,
                                 id_type id )
{
    size_t rmndr = _trade<!BuyMarket>(nullptr, id, size, exec_cb);
    if( rmndr > 0 ){
        throw liquidity_exception( size, rmndr, id, "_insert_market_order()" );
    }
}


SOB_TEMPLATE
template<bool BuyStop>
void
SOB_CLASS::_insert_stop_order( plevel stop,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               id_type id )
{
    /* use stop_limit overload; 0 as limit */
    _insert_stop_order<BuyStop>( stop, 0, size, std::move(exec_cb), id );
}


SOB_TEMPLATE
template<bool BuyStop>
void
SOB_CLASS::_insert_stop_order( plevel stop,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               id_type id )
{
   /*  we need an actual trade @/through the stop, i.e can't assume
       it's already been triggered by where last/bid/ask is...
         - simply pass the order to the appropriate stop chain
         - copy callback functor, needs to persist)                */
    stop->second.emplace( id, stop_bndl(BuyStop, limit, size, exec_cb) );
    _stop_exec<BuyStop>::adjust_state_after_insert(this, stop);
}


SOB_TEMPLATE
void 
SOB_CLASS::_clear_callback_queue()
{
    /* use _busy_with callbacks to abort recursive calls 
         if false, set to true(atomically) 
         if true leave it alone and return */  
    bool busy = false;
    _busy_with_callbacks.compare_exchange_strong(busy,true);
    if( busy ){ 
        return;
    }

    std::vector<dfrd_cb_elem> cb_elems;
    {     
        std::lock_guard<std::mutex> lock(_master_mtx); 
        /* --- CRITICAL SECTION --- */    
        std::move( _deferred_callbacks.begin(),
                   _deferred_callbacks.end(), 
                   back_inserter(cb_elems) );        
        _deferred_callbacks.clear(); 
        /* --- CRITICAL SECTION --- */
    }    
    
    for( const auto & e : cb_elems ){           
        if( e.exec_cb ){ 
            e.exec_cb( e.msg, e.id1, e.id2, e.price, e.sz );
        }
    }        
    _busy_with_callbacks.store(false);
}


/*
 *  CURRENTLY working under the constraint that stop priority goes:  
 *     low price to high for buys                                   
 *     high price to low for sells                                  
 *     buys before sells                                            
 *                                                                  
 *  (The other possibility is FIFO irrespective of price)              
 */

SOB_TEMPLATE
void 
SOB_CLASS::_look_for_triggered_stops(bool nothrow) 
{  /* 
    * PART OF THE ENCLOSING CRITICAL SECTION    
    *
    * we don't check against max/min, because of the cached high/lows 
    */
    try{
        if( !_need_check_for_stops ){
            return;
        }
        _need_check_for_stops = false;

        for( plevel low = _low_buy_stop; low <= _last; ++low ){              
            _handle_triggered_stop_chain<true>(low);         
        }
        for( plevel high = _high_sell_stop; high >= _last; --high ){        
            _handle_triggered_stop_chain<false>(high);           
        }
    }catch(...){
        if( !nothrow )
            throw;
    }
}


SOB_TEMPLATE
template<bool BuyStops>
void 
SOB_CLASS::_handle_triggered_stop_chain(plevel plev)
{  /* 
    * PART OF THE ENCLOSING CRITICAL SECTION 
    */
    order_exec_cb_type cb;
    double limit;         
    size_t sz;
    id_type id_new;
    /*
     * need to copy the relevant chain, delete original, THEN insert
     * if not we can hit the same order more than once / go into infinite loop
     */
    stop_chain_type cchain = std::move(plev->second);
    plev->second.clear();

    _stop_exec<BuyStops>::adjust_state_after_trigger(this, plev);

    for( auto & e : cchain ){
        limit = e.second.limit;
        cb = e.second.exec_cb; 
        sz = e.second.sz;
        id_new = _generate_id();
        
        /* first we handle any advanced conditions */       
        _handle_advanced_order(e.second, e.first, 0);
        
       /*
        * UPDATE! we are creating new id for new exec_cb type (Jan 18)
        * 
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; we simply increment _noutstanding_orders instead
        * and block on that when necessary.
        */    
        if( limit != 0 ){ /* stop to limit */        
            if( cb ){
                _push_exec_callback(callback_msg::stop_to_limit, cb, e.first,
                                    id_new, limit, sz);
            }
            _push_order_no_wait(order_type::limit, e.second.is_buy, limit, 
                                0, sz, cb, order_condition::none, 
                                condition_trigger::fill_partial, nullptr,
                                nullptr, id_new);     
        }else{ /* stop to market */
            if( cb ){ 
                _push_exec_callback(callback_msg::stop_to_market, cb, e.first,
                                    id_new, 0, sz);
            }
            _push_order_no_wait(order_type::market, e.second.is_buy, 0, 
                                0, sz, cb, order_condition::none, 
                                condition_trigger::fill_partial, nullptr,
                                nullptr, id_new);
        }
        // NOTE the new stop drops the advanced order_condition
    }
}


SOB_TEMPLATE
id_type
SOB_CLASS::_push_order_and_wait( order_type oty,
                                 bool buy,
                                 double limit,
                                 double stop,
                                 size_t size,
                                 order_exec_cb_type cb,
                                 order_condition cond,
                                 condition_trigger cond_trigger,
                                 std::unique_ptr<OrderParamaters>&& cond_order_params,
                                 order_admin_cb_type admin_cb,
                                 id_type id )
{

    std::promise<id_type> p;
    std::future<id_type> f(p.get_future());
    _push_order(oty, buy, limit, stop, size, cb, cond, cond_trigger,
                std::move(cond_order_params), admin_cb, id, std::move(p) );

    id_type ret_id;
    try{
        ret_id = f.get(); /* BLOCKING */
    }catch(...){
        _block_on_outstanding_orders(); /* BLOCKING */
        _clear_callback_queue();
        throw;
    }

    _block_on_outstanding_orders(); /* BLOCKING */
    _clear_callback_queue();
    return ret_id;
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order_no_wait( order_type oty,
                                bool buy,
                                double limit,
                                double stop,
                                size_t size,
                                order_exec_cb_type cb,
                                order_condition cond,
                                condition_trigger cond_trigger,
                                std::unique_ptr<OrderParamaters>&& cond_order_params,
                                order_admin_cb_type admin_cb,
                                id_type id )
{
    _push_order( oty, buy, limit, stop, size, cb, cond, cond_trigger,
                std::move(cond_order_params), admin_cb, id,
                /* dummy */ std::move( std::promise<id_type>() ) );
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order( order_type oty,
                        bool buy,
                        double limit,
                        double stop,
                        size_t size,
                        order_exec_cb_type cb,
                        order_condition cond,
                        condition_trigger cond_trigger,
                        std::unique_ptr<OrderParamaters>&& cond_order_params,
                        order_admin_cb_type admin_cb,
                        id_type id,
                        std::promise<id_type>&& p )
{
    {
        std::lock_guard<std::mutex> lock(_order_queue_mtx);
        /* --- CRITICAL SECTION --- */
        _order_queue.push(
            { oty, buy, limit, stop, size, cb, cond, cond_trigger,
              std::move(cond_order_params), admin_cb, id, std::move(p) }
        );
        ++_noutstanding_orders;
        /* --- CRITICAL SECTION --- */
    }
    _order_queue_cond.notify_one();
}


SOB_TEMPLATE
void 
SOB_CLASS::_block_on_outstanding_orders()
{
    while(1){
        {
            std::lock_guard<std::mutex> lock(_order_queue_mtx);
            /* --- CRITICAL SECTION --- */
            if( _noutstanding_orders < 0 ){
                throw std::runtime_error("_noutstanding_orders < 0");
            }else if( _noutstanding_orders == 0 ){
                break;
            }
            /* --- CRITICAL SECTION --- */
        }
        std::this_thread::yield();
    }
}



SOB_TEMPLATE
bool
SOB_CLASS::_pull_order(id_type id, bool pull_linked, bool limits_first)
{
    return limits_first 
        ? (_pull_order<limit_chain_type>(id, pull_linked)
                || _pull_order<stop_chain_type>(id, pull_linked))
        : (_pull_order<stop_chain_type>(id, pull_linked)
                || _pull_order<limit_chain_type>(id, pull_linked));
}


SOB_TEMPLATE
template<typename ChainTy>
bool 
SOB_CLASS::_pull_order(id_type id, bool pull_linked)
{ 
     /*** CALLER MUST HOLD LOCK ON _master_mtx OR RACE CONDTION WITH CALLBACK QUEUE ***/    
    plevel p = _chain<ChainTy>::find(this,id); 
    if( !p ){
        return false;
    }
    _assert_plevel(p);    
    return _pull_order<ChainTy>(id, p, pull_linked);
}


SOB_TEMPLATE
template<typename ChainTy>
bool 
SOB_CLASS::_pull_order(id_type id, plevel p, bool pull_linked)
{ 
     /*** CALLER MUST HOLD LOCK ON _master_mtx OR RACE CONDTION WITH CALLBACK QUEUE ***/    

    ChainTy *c = _chain<ChainTy>::get(p);   
    if( !c ){
        return false;
    }
    
    /* copy bndl before erasing */
    typename ChainTy::mapped_type bndl = c->at(id);
    c->erase(id);
    
    /* adjust cache vals as necessary */
    if( is_limit_chain<ChainTy>() && c->empty() ){
        /*  we can compare vs bid because if we get here and the order is 
            a buy it must be <= the best bid, otherwise its a sell 

           (remember, p is empty if we get here)  */
        (p <= _bid) ? _limit_exec<true>::adjust_state_after_pull(this, p)
                    : _limit_exec<false>::adjust_state_after_pull(this, p);
        
    }else if( !is_limit_chain<ChainTy>() && is_buy_stop(bndl) ){
        if( _stop_exec<true>::stop_chain_is_empty( 
                this, reinterpret_cast<stop_chain_type*>(c) ) )
        {
            _stop_exec<true>::adjust_state_after_pull(this, p);
        }
    }else if( !is_limit_chain<ChainTy>() && !is_buy_stop(bndl) ){
        if( _stop_exec<false>::stop_chain_is_empty(
                this, reinterpret_cast<stop_chain_type*>(c)) )
        {
            _stop_exec<false>::adjust_state_after_pull(this, p);       
        }
    }
       
    _push_exec_callback(callback_msg::cancel, bndl.exec_cb, id, id, 0, 0);
    
    if( pull_linked ){
        _pull_linked_order<ChainTy>(bndl);
    }
    return true;
}


SOB_TEMPLATE
template<typename ChainTy>
void
SOB_CLASS::_pull_linked_order(typename ChainTy::mapped_type& bndl)
{
    order_location *loc = bndl.linked_order;
    if( loc && bndl.cond == order_condition::one_cancels_other ){
        /* pass false to pull_linked since, ostensibly, *this*
           side is in process of being pulled */
        _pull_order(loc->id, loc->price, false, loc->limit_chain);
    }
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy>
std::map<double, typename SOB_CLASS::template _depth<Side>::mapped_type>
SOB_CLASS::_market_depth(size_t depth) const
{
    plevel h;
    plevel l;    
    size_t d;
    std::map<double, typename _depth<Side>::mapped_type> md;
    
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */      
    _high_low<Side>::template get<ChainTy>(this,&h,&l,depth);
    for( ; h >= l; --h){
        if( h->first.empty() ){
            continue;
        }
        d = _chain<limit_chain_type>::size(&h->first);   
        auto v = _depth<Side>::build_value(this,h,d);
        md.insert( std::make_pair(_itop(h), v) );                 
    }
    return md;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy> 
size_t 
SOB_CLASS::_total_depth() const
{
    plevel h;
    plevel l;
    size_t tot = 0;
        
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */    
    _high_low<Side>::template get<ChainTy>(this,&h,&l);
    for( ; h >= l; --h){ 
        tot += _chain<ChainTy>::size( _chain<ChainTy>::get(h) );
    }
    return tot;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename ChainTy>
void
SOB_CLASS::_dump_orders(std::ostream& out,
                        plevel l,
                        plevel h,
                        side_of_trade sot) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    //_high_low<>::range_check(this, &h, &l);

    out << "*** (" << sot << ") " << _chain<ChainTy>::as_order_type()
        << "s ***" << std::endl;
    for( ; h >= l; --h){
        auto c = _chain<ChainTy>::get(h);
        if( !c->empty() ){
            out << _itop(h);
            for( const auto& e : *c ){
               _order::template _dump<ChainTy>::dump(out, e);
            }
            out << std::endl;
        }
    }
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
typename SOB_CLASS::plevel 
SOB_CLASS::_ptoi(TickPrice<TickRatio> price) const
{  /* 
    * the range check asserts in here are 1 position more restrictive to catch
    * bad user price data passed but allow internal index conversions(_itop)
    * 
    * this means that internally we should not convert to a price when
    * a pointer is past beg/at end, signaling a null value
    */   
    long long offset = (price - _base).as_ticks();
    plevel p = _beg + offset;
    assert(p >= _beg);
    assert(p <= (_end-1)); 
    return p;
}


SOB_TEMPLATE 
TickPrice<TickRatio>
SOB_CLASS::_itop(plevel p) const
{   
    _assert_plevel(p); // internal range and align check
    return _base + plevel_offset(p, _beg);
}


SOB_TEMPLATE
double
SOB_CLASS::_tick_price_or_throw(double price, std::string msg) const
{
    if( !is_valid_price(price) ){
        throw std::invalid_argument(msg);
    }
    return price_to_tick(price);
}


SOB_TEMPLATE
void
SOB_CLASS::_reset_internal_pointers( plevel old_beg,
                                     plevel new_beg,
                                     plevel old_end,
                                     plevel new_end,
                                     long long offset )
{   
    /*** PROTECTED BY _master_mtx ***/          
    if( _last ){
        _last = bytes_add(_last, offset);
    }

    /* if plevel is below _beg, it's empty and needs to follow new_beg */
    auto reset_low = [=](plevel *ptr){     
        *ptr = (*ptr == (old_beg-1))  ?  (new_beg - 1) : bytes_add(*ptr, offset);       
    };
    reset_low(&_bid);
    reset_low(&_high_sell_limit);
    reset_low(&_high_buy_stop);
    reset_low(&_high_sell_stop);
     
    /* if plevel is at _end, it's empty and needs to follow new_end */
    auto reset_high = [=](plevel *ptr){     
        *ptr = (*ptr == old_end)  ?  new_end : bytes_add(*ptr, offset);         
    };
    reset_high(&_ask);
    reset_high(&_low_buy_limit);
    reset_high(&_low_buy_stop);
    reset_high(&_low_sell_stop);
}


SOB_TEMPLATE
void
SOB_CLASS::_grow_book(TickPrice<TickRatio> min, size_t incr, bool at_beg)
{
    if( incr == 0 ){
        return;
    }

    plevel old_beg = _beg;
    plevel old_end = _end;
    size_t old_sz = _book.size();

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    /* after this point no guarantee about cached pointers */
    _book.insert( at_beg ? _book.begin() : _book.end(),
                  incr,
                  chain_pair_type() );
    /* the book is in an INVALID state until _reset_internal_pointers returns */

    _base = min;
    _beg = &(*_book.begin()) + 1;
    _end = &(*_book.end());

    long long offset = at_beg ? bytes_offset(_end, old_end)
                              : bytes_offset(_beg, old_beg);

    assert( equal( 
        bytes_offset(_end, _beg), 
        bytes_offset(old_end, old_beg) 
            + static_cast<long long>(sizeof(*_beg) * incr),  
        static_cast<long long>((old_sz + incr - 1) * sizeof(*_beg)),
        static_cast<long long>((_book.size() - 1) * sizeof(*_beg))
    ) );
    
    _reset_internal_pointers(old_beg, _beg, old_end, _end, offset);
    _assert_internal_pointers();    
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
void
SOB_CLASS::_assert_plevel(plevel p) const
{
    assert( (labs(bytes_offset(p, _beg)) % sizeof(chain_pair_type)) == 0 );
    assert( (labs(bytes_offset(p, _end)) % sizeof(chain_pair_type)) == 0 );
    assert( p >= (_beg - 1) );
    assert( p <= _end );
}


SOB_TEMPLATE
void
SOB_CLASS::_assert_internal_pointers() const
{
#ifndef NDEBUG  
    if( _last ){
        _assert_plevel(_last);
    }
    _assert_plevel(_bid);
    _assert_plevel(_ask);
    _assert_plevel(_low_buy_limit);
    _assert_plevel(_high_sell_limit);
    _assert_plevel(_low_buy_stop);
    _assert_plevel(_high_buy_stop);
    _assert_plevel(_low_sell_stop);
    _assert_plevel(_high_sell_stop);
    if( _bid != (_beg-1) ){
        if( _ask != _end ){
            assert( _ask > _bid );
        }
        if( _low_buy_limit != _end ){
            assert( _low_buy_limit <= _bid);
        }
    }
    if( _high_sell_limit != (_beg-1) ){
        if( _ask != _end ){
            assert( _high_sell_limit >= _ask );
        }
        if( _low_buy_limit != _end ){
            assert( _high_sell_limit > _low_buy_limit );
        }
    }
    if( _low_buy_stop != _end || _high_buy_stop != (_beg-1) ){  // OR
        assert( _high_buy_stop >= _low_buy_stop );
    }
    if( _low_sell_stop != _end || _high_sell_stop != (_beg-1) ){ // OR
        assert( _high_sell_stop >= _low_sell_stop );
    }
#endif
}

SOB_TEMPLATE
std::unique_ptr<OrderParamaters>
SOB_CLASS::_build_aot_order(const OrderParamaters& order) const
{
    // _master_mtx should needs to be held for this
    double limit = 0.0;
    double stop = 0.0;
    try{
        switch( order.get_order_type() ){
        case order_type::stop_limit:
            stop = _tick_price_or_throw(order.stop(), "invalid stop price");
            /* no break */
        case order_type::limit:
            limit = _tick_price_or_throw(order.limit(), "invalid limit price");
            break;
        case order_type::stop:
            stop = _tick_price_or_throw(order.stop(), "invalid stop price");
            break;
        case order_type::market:
            break;
        default:
            throw advanced_order_error("invalid order type");
        };
    }catch(std::invalid_argument& e){
        throw advanced_order_error(e);
    }
    return std::unique_ptr<OrderParamaters>(
            new OrderParamaters(order.is_buy(), order.size(), limit, stop)
    );
}


SOB_TEMPLATE
void
SOB_CLASS::_check_oco_limit_order( bool buy,
                                   double limit,
                                   std::unique_ptr<OrderParamaters> & op )
{
    order_type ot = op->get_order_type();
    if( ot == order_type::market ){
        throw advanced_order_error("OCO limit/market not valid order type");
    }else if( ot != order_type::limit ){
        return;
    }

    if( buy && !op->is_buy() && limit >= op->limit() ){
        throw advanced_order_error("OCO limit/limit buy price >= sell price");
    }else if( !buy && op->is_buy() && limit <= op->limit() ){
        throw advanced_order_error("OCO limit/limit sell price <= buy price");
    }else if( op->limit() == limit ){
         throw advanced_order_error("OCO limit/limit of same price" );
    }
}

// TODO create a cleaner mechanism for checking aots/conds against insert calls
SOB_TEMPLATE
id_type 
SOB_CLASS::insert_limit_order( bool buy,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               const AdvancedOrderTicket& advanced,
                               order_admin_cb_type admin_cb ) 
{      
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }
   
    std::unique_ptr<OrderParamaters> cparams;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        limit = _tick_price_or_throw(limit, "invalid limit price");
        if( advanced
            && (advanced.condition() != order_condition::fill_or_kill)
            && (advanced.condition() != order_condition::none) )
        {
            cparams = _build_aot_order(advanced.order1());
            if( advanced.condition() == order_condition::one_cancels_other ){
                _check_oco_limit_order(buy, limit, cparams);
            }

        }
        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(order_type::limit, buy, limit, 0, size, exec_cb, 
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams), admin_cb);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_market_order( bool buy,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                const AdvancedOrderTicket& advanced,
                                order_admin_cb_type admin_cb )
{    
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }
    std::unique_ptr<OrderParamaters> cparams;
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        if( advanced ){
            switch( advanced.condition() ){
            case order_condition::one_cancels_other:
                throw advanced_order_error(
                        "OCO invalid condition for market order"
                        );
            case order_condition::fill_or_kill:
                throw advanced_order_error(
                        "FOK invliad condition for market order"
                        );
            default:
                cparams = _build_aot_order(advanced.order1());
            };
        }
        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(order_type::market, buy, 0, 0, size, exec_cb, 
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams), admin_cb);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced,
                              order_admin_cb_type admin_cb )
{
    return insert_stop_order(buy, stop, 0, size, exec_cb, advanced, admin_cb);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              double limit,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              const AdvancedOrderTicket& advanced,
                              order_admin_cb_type admin_cb )
{      
    if(size == 0){
        throw std::invalid_argument("invalid order size");
    }
    if( advanced.condition() == order_condition::fill_or_kill ){
        throw advanced_order_error("FOK invliad condition for market order");
    }
    std::unique_ptr<OrderParamaters> cparams;
    order_type ot = order_type::stop;    
    {
        std::lock_guard<std::mutex> lock(_master_mtx);
        /* --- CRITICAL SECTION --- */
        stop = _tick_price_or_throw(stop, "invalid stop price");         
        if( limit != 0 ){
            limit = _tick_price_or_throw(limit, "invalid limit price");
            ot = order_type::stop_limit;
        }
        if( advanced ){
            cparams = _build_aot_order(advanced.order1());
            if( cparams->stop() != 0.0
                && cparams->stop() == stop ){
                throw advanced_order_error("stop orders of same price");
            }
        }
        /* --- CRITICAL SECTION --- */
    }
    return _push_order_and_wait(ot, buy, limit, stop, size, exec_cb,
                                advanced.condition(), advanced.trigger(),
                                std::move(cparams), admin_cb);
}


SOB_TEMPLATE
bool 
SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{
    if(id == 0){
        throw std::invalid_argument("invalid order id(0)");
    }
    return _push_order_and_wait(order_type::null, search_limits_first, 
                                0, 0, 0, nullptr, order_condition::none,
                                condition_trigger::none, nullptr, nullptr, id); 
}


SOB_TEMPLATE
bool
SOB_CLASS::pull_order_hint(id_type id, double price, bool search_limits_first)
{
    if(id == 0){
        throw std::invalid_argument("invalid order id(0)");
    }
    return _push_order_and_wait(order_type::null, search_limits_first,
                                price, 0, 0, nullptr, order_condition::none,
                                condition_trigger::none, nullptr, nullptr, id);
}


SOB_TEMPLATE
order_info
SOB_CLASS::get_order_info(id_type id, bool search_limits_first) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return search_limits_first
            ? _order::template info<limit_chain_type, stop_chain_type>(this, id)
            : _order::template info<stop_chain_type, limit_chain_type>(this, id);
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
order_info
SOB_CLASS::get_order_info_hint( id_type id,
                                double price,
                                bool search_limits_first ) const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    order_info oi = {};
    if( is_valid_price(price) ){
        oi = search_limits_first
            ? _order::template info<limit_chain_type, stop_chain_type>(this, id, price)
            : _order::template info<stop_chain_type, limit_chain_type>(this, id, price);
    }
    if( !oi ){
        oi = search_limits_first
            ? _order::template info<limit_chain_type, stop_chain_type>(this, id)
            : _order::template info<stop_chain_type, limit_chain_type>(this, id);
    }
    return oi;
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_limit_order( id_type id,
                                     bool buy,
                                     double limit,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     const AdvancedOrderTicket& advanced,
                                     order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_limit_order(buy, limit, size, exec_cb, advanced, 
                                    admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_market_order( id_type id,
                                      bool buy,
                                      size_t size,
                                      order_exec_cb_type exec_cb,
                                      const AdvancedOrderTicket& advanced,
                                      order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_market_order(buy, size, exec_cb, advanced, admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    double stop,
                                    size_t size,
                                    order_exec_cb_type exec_cb,
                                    const AdvancedOrderTicket& advanced,
                                    order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, size, exec_cb, advanced, admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    double stop,
                                    double limit,
                                    size_t size,
                                    order_exec_cb_type exec_cb,
                                    const AdvancedOrderTicket& advanced,
                                    order_admin_cb_type admin_cb)
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, limit, size, exec_cb, advanced, 
                                   admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
void 
SOB_CLASS::grow_book_above(double new_max)
{
    auto diff = TickPrice<TickRatio>(new_max) - max_price();

    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_max too far from old max to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(_base, incr, false);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::grow_book_below(double new_min) 
{
    if( _base == 1 ){ // can't go any lower
        return;
    }

    TickPrice<TickRatio> new_base(new_min);
    if( new_base < 1 ){
        new_base = TickPrice<TickRatio>(1);
    }

    auto diff = _base - new_base;
    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_min too far from old min to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(new_base, incr, true);
    }
}


SOB_TEMPLATE
void 
SOB_CLASS::dump_internal_pointers(std::ostream& out) const
{    
    auto println = [&](std::string n, plevel p){
        std::string price;
        try{
            price = std::to_string(_itop(p));
        }catch(std::range_error&){  
            price = "N/A";
        }        
        out<< std::setw(18) << n << " : " 
           << std::setw(14) << price << " : "
           << std::hex << p << std::dec << std::endl;
    };   

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::ios sstate(nullptr);
    sstate.copyfmt(out);
    out<< "*** CACHED PLEVELS ***" << std::left << std::endl;
    println("_end", _end);
    println("_high_sell_limit", _high_sell_limit);
    println("_high_buy_stop", _high_buy_stop);
    println("_low_buy_stop", _low_buy_stop);
    println("_ask", _ask);
    println("_last", _last);
    println("_bid", _bid);
    println("_high_sell_stop", _high_sell_stop);
    println("_low_sell_stop", _low_sell_stop);
    println("_low_buy_limit", _low_buy_limit);
    println("_beg", _beg);
    out.copyfmt(sstate);
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl()
     :
        _order_bndl(0, nullptr)
     {    
     }

SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl( size_t sz,
                                     order_exec_cb_type exec_cb,
                                     order_condition cond,
                                     condition_trigger trigger )
    :
        sz(sz),
        exec_cb(exec_cb),
        cond(cond),
        trigger(trigger)
    {
        switch(cond){
        case order_condition::one_cancels_other:
            linked_order = nullptr;
            break;
        case order_condition::one_triggers_other:
            contingent_order = nullptr;
            break;
        case order_condition::none:
            _reserved = nullptr;         
            break;
        default:
            throw new std::runtime_error("invalid order condition");
        }       
    }

SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl(const _order_bndl& bndl)
   :
       sz(bndl.sz),
       exec_cb(bndl.exec_cb),
       cond(bndl.cond),
       trigger(bndl.trigger)
   {
       switch(cond){
       case order_condition::one_cancels_other:
           linked_order = bndl.linked_order
                        ? new order_location(*bndl.linked_order)
                        : nullptr;
           break;
       case order_condition::one_triggers_other:
           contingent_order = bndl.contingent_order
                            ? new OrderParamaters(*bndl.contingent_order)
                            : nullptr;
           break;
       case order_condition::none:           
           assert( bndl._reserved == nullptr ); // NOT IMPLEMENTED
           _reserved = nullptr;
           break;
       default:
           throw new std::runtime_error("invalid order condition");
       }   
   }


SOB_TEMPLATE
SOB_CLASS::_order_bndl::_order_bndl(_order_bndl&& bndl)
   :
       sz(bndl.sz),
       exec_cb(bndl.exec_cb),
       cond(bndl.cond),
       trigger(bndl.trigger)
   {
       switch(cond){
       case order_condition::one_cancels_other:
           linked_order = bndl.linked_order;
           bndl.linked_order = nullptr;
           break;
       case order_condition::one_triggers_other:
           contingent_order = bndl.contingent_order;
           bndl.contingent_order = nullptr;
           break;
       case order_condition::none:
           assert( bndl._reserved == nullptr ); // NOT IMPLEMENTED
           _reserved = bndl._reserved;
           //bndl._reserved = nullptr;
           break;
       default:
           throw new std::runtime_error("invalid order condition");
       }       
   }


SOB_TEMPLATE
SOB_CLASS::_order_bndl::~_order_bndl()
   {
       switch(cond){
       case order_condition::one_cancels_other:
           if( linked_order ){
               delete linked_order;
           }
           break;
       case order_condition::one_triggers_other:
           if( contingent_order ){
               delete contingent_order;
           }
           break;
       case order_condition::none:
           assert( _reserved == nullptr ); // NOT IMPLEMENTED
           //if( _reserved ){
           //    delete _reserved;
           //}
           break;
       default:
           throw new std::runtime_error("invalid order condition");
       }     
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl()
    :
        _order_bndl(),
        is_buy(),
        limit()
    {
    }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl( bool is_buy, 
                                 double limit, 
                                 size_t sz,
                                 order_exec_cb_type exec_cb, 
                                 order_condition cond,
                                 condition_trigger trigger )
   :
       _order_bndl(sz, exec_cb, cond),
       is_buy(is_buy),
       limit(limit)
   {
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl( const stop_bndl& bndl )
   :
       _order_bndl(bndl),
       is_buy(bndl.is_buy),
       limit(bndl.limit)
   {
   }

SOB_TEMPLATE
SOB_CLASS::stop_bndl::stop_bndl( stop_bndl&& bndl )
   :
        _order_bndl( std::move(bndl) ),
        is_buy(bndl.is_buy),
        limit(bndl.limit)
   {
   }
};

#undef SOB_TEMPLATE
#undef SOB_CLASS
