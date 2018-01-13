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
#include "simpleorderbook.hpp"

/* readability macros - #undef'd at the end of file */
#define T_(tuple,i) std::get<i>(tuple)
#define SOB_TEMPLATE template<typename TickRatio, size_t MaxMemory>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio,MaxMemory>

namespace sob{

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbookImpl(TrimmedRational<TickRatio> min, size_t incr)
    :  
        _bid_size(0),
        _ask_size(0),
        _last_size(0),    
        /* lowest price */
        _base(min),
        /* actual orderbook object */
        _book(incr + 1), /*pad the beg side */
        /***********************************************************************
         :: our ersatz iterator approach ::
         
         i = [ 0, _total_incr ) 
     
         vector iter    [begin()]                                      [ end() ]
         internal pntr  [ _base ][ _beg ]                  [ _end - 1 ][ _end  ]
         internal index [ NULL  ][   i  ][ i+1 ]...   [ _total_incr-1 ][  NULL ]
         external price [ THROW ][ min  ]                     [  max  ][ THROW ]        
    
        ***********************************************************************/
        _beg( &(*_book.begin()) + 1 ), 
        _end( &(*_book.end())), 
        _last( 0 ), // _beg + _lower_incr ), *** CHANGED *** 
        _bid( &(*(_beg-1)) ),
        _ask( &(*_end) ),
        /* cache range vals for faster lookups */
        _low_buy_limit( &(*_end) ),
        _high_sell_limit( &(*(_beg-1)) ),
        _low_buy_stop( &(*_end) ),
        _high_buy_stop( &(*(_beg-1)) ),
        _low_sell_stop( &(*_end) ),
        _high_sell_stop( &(*(_beg-1)) ),
        /* internal trade stats */
        _total_volume(0),
        _last_id(0), 
        _timesales(),                  
        /* trade callbacks */
        _deferred_callback_queue(), 
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
                _order_queue.push(order_queue_elem_type()); 
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
 * _high_low::range_check : bounds check and reset plevels if necessary
 * 
 * _high_low::set_using_depth : populate plevels using passed depth 
 *     from 'inside' bid/ask and internal bounds
 *     
 * _high_low::set_using_cached : populate plevels using cached extremes
 * 
 * (note: _high_low specials inherit from non-special to access range_check())
 */
SOB_TEMPLATE
template<side_of_market Side, typename My> 
struct SOB_CLASS::_high_low {
    typedef typename SOB_CLASS::plevel plevel;
private:
    template<typename DummyChainTY, typename Dummy=void> 
    struct _set_using_cached;    

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::limit_chain_type,Dummy>{
    public:
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {   /* Nov 20 2016 - add min/max */
            *pl = (plevel)min(sob->_low_buy_limit,sob->_ask);
            *ph = (plevel)max(sob->_high_sell_limit,sob->_bid); 
        }
    };

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::stop_chain_type,Dummy>{
    public:
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {
            *pl = (plevel)min( (plevel)min( sob->_low_sell_stop, 
                                            sob->_low_buy_stop ),
                               (plevel)min( sob->_high_sell_stop, 
                                            sob->_high_buy_stop ) );

            *ph = (plevel)max( (plevel)max( sob->_low_sell_stop, 
                                            sob->_low_buy_stop ),
                               (plevel)max( sob->_high_sell_stop, 
                                            sob->_high_buy_stop ) ); 
        }
    };

public:     
    static inline void 
    range_check(const My* sob, plevel* ph, plevel* pl)
    {
        *ph = (*ph >= sob->_end) ? sob->_end - 1 : *ph;
        *pl = (*pl < sob->_beg) ? sob->_beg : *pl;
    }

    template<typename ChainTy> 
    static void 
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_t depth )
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl); 
        *ph = (plevel)min(sob->_ask + depth - 1, *ph);
        *pl = (plevel)max(sob->_bid - depth +1, *pl);    
        _high_low<Side,My>::range_check(sob,ph,pl);
    }

    template<typename ChainTy> 
    static inline void 
    set_using_cached(const My* sob, plevel* ph, plevel *pl)
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl);
        _high_low<Side,My>::range_check(sob,ph,pl);
    }
};


SOB_TEMPLATE
template<typename My> 
struct SOB_CLASS::_high_low<side_of_market::bid,My>
        : public _high_low<side_of_market::both,My> {
    typedef typename SOB_CLASS::plevel plevel;
private:
    template<typename DummyChainTY, typename Dummy=void> 
    struct _set_using_cached;    

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::limit_chain_type,Dummy>{
    public:
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {
            *pl = sob->_low_buy_limit;
            *ph = sob->_bid; 
        }
    };

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::stop_chain_type,Dummy>{
    public: /* use the side_of_market::both version in base */
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {            
            _high_low<side_of_market::both,My>::template
            set_using_cached<typename SOB_CLASS::stop_chain_type>::call(sob,ph,pl);
        }
    };

public: 
    template<typename ChainTy> 
    static void 
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl);     
        *ph = sob->_bid;     
        *pl = (plevel)max(sob->_bid - depth +1, *pl);
        _high_low<side_of_market::both,My>::range_check(sob,ph,pl);
    }

    template<typename ChainTy> 
    static inline void 
    set_using_cached(const My* sob, plevel* ph, plevel *pl)
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl);
        _high_low<side_of_market::both,My>::range_check(sob,ph,pl);
    }
};


SOB_TEMPLATE
template<typename My> 
struct SOB_CLASS::_high_low<side_of_market::ask,My>
        : public _high_low<side_of_market::both,My> {
    typedef typename SOB_CLASS::plevel plevel;
private:
    template<typename DummyChainTY, typename Dummy=void> 
    struct _set_using_cached;    

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::limit_chain_type,Dummy>{
    public:
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {
            *pl = sob->_ask;
            *ph = sob->_high_sell_limit; 
        }
    };

    template<typename Dummy> 
    struct _set_using_cached<typename SOB_CLASS::stop_chain_type,Dummy>{
    public: /* use the side_of_market::both version in base */
        static inline void 
        call(const My* sob,plevel* ph,plevel *pl)
        {            
            _high_low<side_of_market::both,My>::template 
            set_using_cached<typename SOB_CLASS::stop_chain_type>::call(sob,ph,pl);
        }
    };

public: 
    template<typename ChainTy> 
    static void 
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_t depth)
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl);    
        *pl = sob->_ask;
        *ph = (plevel)min(sob->_ask + depth - 1, *ph);
        _high_low<side_of_market::both,My>::range_check(sob,ph,pl);
    }

    template<typename ChainTy> 
    static inline void 
    set_using_cached(const My* sob, plevel* ph, plevel *pl)
    {
        _set_using_cached<ChainTy>::call(sob,ph,pl);
        _high_low<side_of_market::both,My>::range_check(sob,ph,pl);
    }
};

SOB_TEMPLATE
template<side_of_market Side, typename My>
struct SOB_CLASS::_depth{
    static inline 
    size_t 
    build_value(const My* sob, typename SOB_CLASS::plevel p, size_t d){
        return d;
    }    
};

SOB_TEMPLATE
template<typename My>
struct SOB_CLASS::_depth<side_of_market::both, My>{
    static inline 
    std::pair<size_t, side_of_market> 
    build_value(const My* sob, typename SOB_CLASS::plevel p, size_t d){
        auto s = (p >= sob->_ask) ? side_of_market::ask : side_of_market::bid;
        return std::make_pair(d,s);          
    }
};


/*
 * _order_info::generate : generate specialized order_info_type tuples
 */ 
SOB_TEMPLATE
template<typename ChainTy, typename My> 
struct SOB_CLASS::_order_info{
    static inline order_info_type 
    generate()
    { return order_info_type(order_type::null,false,0,0,0); }
};


SOB_TEMPLATE
template<typename My> 
struct SOB_CLASS::_order_info<typename SOB_CLASS::limit_chain_type, My>{
    typedef typename SOB_CLASS::plevel plevel;

    static inline order_info_type 
    generate(const My* sob, 
             id_type id, 
             plevel p, 
             typename SOB_CLASS::limit_chain_type* c)
    {
        return order_info_type(
                    order_type::limit, (p < sob->_ask), sob->_itop(p), 
                    0, T_(c->at(id),0)
                );
    } 
};


SOB_TEMPLATE
template<typename My> 
struct SOB_CLASS::_order_info<typename SOB_CLASS::stop_chain_type, My>{    
    typedef typename SOB_CLASS::plevel plevel;

    static order_info_type 
    generate(const My* sob, 
             id_type id, 
             plevel p, 
             typename SOB_CLASS::stop_chain_type* c)
    {
        auto bndl = c->at(id);
        plevel stop_limit_plevel = (plevel)T_(bndl,1);        
        if( stop_limit_plevel ){    
            return order_info_type(
                        order_type::stop_limit, T_(bndl,0), sob->_itop(p), 
                        sob->_itop(stop_limit_plevel), T_(bndl,2)
                   );
        }        
        return order_info_type(
                    order_type::stop, T_(bndl,0), sob->_itop(p), 0, T_(bndl,2)
               );
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
    template<typename InnerChainTy, typename My>
    static std::pair<typename SOB_CLASS::plevel,InnerChainTy*> 
    find(const My* sob, id_type id)
    {         
        InnerChainTy* c;
        plevel beg;
        plevel end;
        _high_low<>::template set_using_cached<InnerChainTy>(sob,&end,&beg);       
        for( ; beg <= end; ++beg ){
            c = _chain<InnerChainTy>::get(beg);
            for( auto & e : *c) {
                if( e.first == id ){ 
                    return std::pair<plevel,InnerChainTy*>(beg,c);
                }
            }
        }       
        return std::pair<typename SOB_CLASS::plevel,InnerChainTy*>(nullptr,nullptr);
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
    { 
        size_t sz = 0;
        for( auto & e : *c ){
            sz += e.second.first;
        }
        return sz;
    }  
  
    template<typename My>
    static inline std::pair<typename SOB_CLASS::plevel,chain_type*>    
    find(const My* sob, id_type id)
    { return _chain<void>::template find<chain_type>(sob,id); }
};


SOB_TEMPLATE
template<typename Dummy> 
struct SOB_CLASS::_chain<typename SOB_CLASS::stop_chain_type, Dummy>
        : public _chain<void> { 
    typedef typename SOB_CLASS::stop_chain_type chain_type;

    static inline chain_type* 
    get(typename SOB_CLASS::plevel p)
    { return &(p->second); }

    static size_t 
    size(chain_type* c)
    {
        size_t sz = 0;
        for( auto & e : *c ){
            sz += T_(e.second,2);
        }
        return sz;
    }    

    template<typename My>
    static inline std::pair<typename SOB_CLASS::plevel,chain_type*> 
    find(const My* sob, id_type id)
    { return _chain<void>::template find<chain_type>(sob,id); }
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
    template<typename My>
    static inline bool
    is_executable_chain(const My* sob, typename SOB_CLASS::plevel p) 
    { return (p <= sob->_bid || !p) && (sob->_bid >= sob->_beg); }

    template<typename My>
    static inline typename SOB_CLASS::plevel
    get_inside(const My* sob) 
    { return sob->_bid; }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    template<typename My>
    static bool
    find_new_best_inside(My* sob) 
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
    template<typename My>
    static inline void
    _jump_to_nonempty_chain(My* sob) 
    {
        for( ; 
             sob->_bid->first.empty() && (sob->_bid >= sob->_beg); 
             --sob->_bid )
           {  
           } 
    }

    template<typename My>
    static bool
    _check_and_reset_size(My* sob)
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

    template<typename My>
    static inline void
    _adjust_limit_cache(My* sob) 
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

    template<typename My>
    static inline bool
    is_executable_chain(const My* sob, typename SOB_CLASS::plevel p) 
    { return (p >= sob->_ask || !p) && (sob->_ask < sob->_end); }

    template<typename My>
    static inline typename SOB_CLASS::plevel
    get_inside(const My* sob) 
    { return sob->_ask; }

private:
    template<typename My>
    static inline void
    _jump_to_nonempty_chain(My* sob) 
    {
        for( ; 
             sob->_ask->first.empty() && (sob->_ask < sob->_end); 
             ++sob->_ask ) 
            { 
            }  
    }

    template<typename My>
    static bool
    _check_and_reset_size(My* sob) 
    {
        if( sob->_ask >= sob->_end ){
            sob->_ask_size = 0;          
            sob->_high_sell_limit = sob->_beg-1;    
            return false;
        }
        sob->_ask_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                         ::size(&sob->_ask->first);                     
        return true;
    }

    template<typename My>
    static inline void
    _adjust_limit_cache(My* sob) 
    {       
        if( sob->_ask > sob->_high_sell_limit ){
            sob->_high_sell_limit = sob->_ask;
        }
    }
};


SOB_TEMPLATE
template<bool BuyLimit, typename Dummy> 
struct SOB_CLASS::_limit_exec {
    template<typename My>
    static void
    adjust_state_after_pull(My* sob, SOB_CLASS::plevel limit) 
    {
        if( limit < sob->_low_buy_limit ){ 
            throw invalid_state("can't remove limit lower than cached val");
        }
        if( limit == sob->_low_buy_limit ){
            ++sob->_low_buy_limit;  /*dont look for next valid plevel*/
        } 
        if( limit == sob->_bid ){
            SOB_CLASS::_core_exec<true>::find_new_best_inside(sob);
        }
    }

    template<typename My>
    static void
    adjust_state_after_insert(My* sob, 
                              SOB_CLASS::plevel limit, 
                              SOB_CLASS::limit_chain_type* orders) 
    {
        if( limit >= sob->_bid ){
            sob->_bid = limit;
            sob->_bid_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                             ::size(orders);
        }

        if( limit < sob->_low_buy_limit ){
            sob->_low_buy_limit = limit;  
        }
    }
};


SOB_TEMPLATE 
template<typename Dummy>
struct SOB_CLASS::_limit_exec<false, Dummy> {
    template<typename My>
    static void
    adjust_state_after_pull(My* sob, SOB_CLASS::plevel limit) 
    {
        if( limit > sob->_high_sell_limit ){ 
            throw invalid_state("can't remove limit higher than cached val");
        }
        if( limit == sob->_high_sell_limit ){
            --sob->_high_sell_limit;  /*dont look for next valid plevel*/
        }
        if( limit == sob->_ask ){
            SOB_CLASS::_core_exec<false>::find_new_best_inside(sob);
        }
    }

    template<typename My>
    static void
    adjust_state_after_insert(My* sob, 
                              SOB_CLASS::plevel limit, 
                              SOB_CLASS::limit_chain_type* orders) 
    {
        if( limit <= sob->_ask) {
            sob->_ask = limit;
            sob->_ask_size = SOB_CLASS::_chain<SOB_CLASS::limit_chain_type>
                             ::size(orders);
        }
        if( limit > sob->_high_sell_limit ){
            sob->_high_sell_limit = limit; 
        }
    }
};


SOB_TEMPLATE
template<bool BuyStop, bool Redirect /* = BuyStop */> 
struct SOB_CLASS::_stop_exec {
    template<typename My> 
    static void
    adjust_state_after_pull(My* sob, SOB_CLASS::plevel stop) 
    {
        // COULD WE ACCIDENTALLY ADJUST TWICE IN HERE ???
        if( stop > sob->_high_buy_stop ){                  
            throw invalid_state("can't remove stop higher than cached val");
        }
        if( stop < sob->_low_buy_stop ){
            throw invalid_state("can't remove stop lower than cached val");
        }
        
        if( stop == sob->_high_buy_stop ){
            --(sob->_high_buy_stop); /*dont look for next valid plevel*/
        }     
        if( stop == sob->_low_buy_stop ){
            ++(sob->_low_buy_stop); /*dont look for next valid plevel*/
        }
    }

    template<typename My> 
    static void
    adjust_state_after_insert(My* sob, SOB_CLASS::plevel stop) 
    {
        if( stop < sob->_low_buy_stop ){    
            sob->_low_buy_stop = stop;
        }
        if( stop > sob->_high_buy_stop ){ 
            sob->_high_buy_stop = stop;  
        }
    }

    template<typename My> 
    static void
    adjust_state_after_trigger(My* sob, SOB_CLASS::plevel stop) 
    {    
        sob->_low_buy_stop = stop + 1;              
        if( sob->_low_buy_stop > sob->_high_buy_stop ){            
            sob->_low_buy_stop = sob->_end;
            sob->_high_buy_stop = sob->_beg - 1;
        }
    }

    /* THIS WILL GET IMPORTED BY THE <false> specialization */
    template<typename My>
    static bool
    stop_chain_is_empty(My* sob, SOB_CLASS::stop_chain_type* c)
    {
        static auto ifcond = 
            [](const SOB_CLASS::stop_chain_type::value_type & v) 
            { 
                return T_(v.second,0) == Redirect; 
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
    template<typename My> 
    static void
    adjust_state_after_pull(My* sob, SOB_CLASS::plevel stop) 
    {
        if( stop > sob->_high_sell_stop ){ 
            throw invalid_state("can't remove stop higher than cached val");
        }
        if( stop < sob->_low_sell_stop ){ 
            throw invalid_state("can't remove stop lower than cached val");
        }
        
        if( stop == sob->_high_sell_stop ){
            --(sob->_high_sell_stop); /*dont look for next valid plevel */
        }              
        if( stop == sob->_low_sell_stop ){
            ++(sob->_low_sell_stop); /*dont look for next valid plevel*/
        }
    }

    template<typename My> 
    static void
    adjust_state_after_insert(My* sob, SOB_CLASS::plevel stop) 
    {
        if( stop > sob->_high_sell_stop ){ 
            sob->_high_sell_stop = stop;
        }
        if( stop < sob->_low_sell_stop ){    
            sob->_low_sell_stop = stop;  
        }
    }

    template<typename My> 
    static void
    adjust_state_after_trigger(My* sob, SOB_CLASS::plevel stop) 
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
    for( auto & elem : plev->first ){        
        amount = std::min(size, elem.second.first);
        /* push callbacks into queue; update state */
        _trade_has_occured( plev, amount, id, elem.first, exec_cb, 
                            elem.second.second, true );
        /* reduce the amount left to trade */ 
        size -= amount;    
        rmndr = elem.second.first - amount;
        if( rmndr > 0 ){ 
            elem.second.first = rmndr; /* adjust outstanding order size */
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
                               order_exec_cb_type& cbsell,
                               bool took_offer )
{  
    /* CAREFUL: we can't insert orders from here since we have yet to finish
       processing the initial order (possible infinite loop); */  
    double p = _itop(plev);  
    
    /* buy side */
    _deferred_callback_queue.push_back( 
        dfrd_cb_elem_type( callback_msg::fill, cbbuy, idbuy, p, size )
    );
    /* sell side */
    _deferred_callback_queue.push_back( 
        dfrd_cb_elem_type( callback_msg::fill, cbsell, idsell, p, size )
    );

    _timesales.push_back( timesale_entry_type(clock_type::now(),p,size) );
    _last = plev;
    _total_volume += size;
    _last_size = size;
    _need_check_for_stops = true;
}


SOB_TEMPLATE
void 
SOB_CLASS::_threaded_order_dispatcher()
{                 
    for( ; ; ){
        order_queue_elem_type e;
        {
            std::unique_lock<std::mutex> lock(_order_queue_mtx);      
            _order_queue_cond.wait( 
                lock, 
                [this]{ return !this->_order_queue.empty(); }
            );

            e = std::move(_order_queue.front());
            _order_queue.pop(); 
            if( !_master_run_flag ){
                if( _noutstanding_orders == 0){
                    break;
                }
                throw std::logic_error("invalid state: _noutstanding_orders != 0");                              
            }
        }         
        
        std::promise<id_type> p = std::move( T_(e,8) );        
        id_type id = T_(e,6);
        if( !id ){ 
            id = _generate_id();
        }
        
        try{
            _route_order(e,id);
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
SOB_CLASS::_route_order(order_queue_elem_type& e, id_type& id)
{
    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */
    try{
        switch( T_(e,0) ){            
        case order_type::limit:         
            T_(e,1)       
                ? _insert_limit_order<true>(T_(e,2), T_(e,4), T_(e,5), id, T_(e,7))
                : _insert_limit_order<false>(T_(e,2), T_(e,4), T_(e,5), id, T_(e,7));                         
            _look_for_triggered_stops(false); /* throw */      
            break;
       
        case order_type::market:                
            T_(e,1)
                ? _insert_market_order<true>(T_(e,4), T_(e,5), id, T_(e,7))
                : _insert_market_order<false>(T_(e,4), T_(e,5), id, T_(e,7));                                                      
            _look_for_triggered_stops(false); /* throw */               
            break;
      
        case order_type::stop:        
            T_(e,1)
                ? _insert_stop_order<true>(T_(e,3), T_(e,4), T_(e,5), id, T_(e,7))
                : _insert_stop_order<false>(T_(e,3), T_(e,4), T_(e,5), id, T_(e,7));
            break;
         
        case order_type::stop_limit:        
            T_(e,1)
                ? _insert_stop_order<true>(T_(e,3), T_(e,2), T_(e,4), T_(e,5), id, T_(e,7))
                : _insert_stop_order<false>(T_(e,3), T_(e,2), T_(e,4), T_(e,5), id, T_(e,7));
            break;
         
        case order_type::null: 
            /* not the cleanest but most effective/thread-safe 
               e[1] indicates to check limits first (not buy/sell) */
            id = (id_type)_pull_order(T_(e,1),id);
            break;
        
        default: 
            throw std::runtime_error("invalid order type in order_queue");
        }
    }catch(...){                
        _look_for_triggered_stops(true); /* no throw */
        throw;
    }             
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
id_type 
SOB_CLASS::_push_order_and_wait( order_type oty, 
                                 bool buy, 
                                 plevel limit,
                                 plevel stop,
                                 size_t size,
                                 order_exec_cb_type cb,                                              
                                 order_admin_cb_type admin_cb,
                                 id_type id )
{
    
    std::promise<id_type> p;
    std::future<id_type> f(p.get_future());    
    {
         std::lock_guard<std::mutex> lock(_order_queue_mtx);
         /* --- CRITICAL SECTION --- */
         _order_queue.push(
             order_queue_elem_type(
                 oty, buy, limit, stop, size, cb, id, admin_cb, std::move(p)
             ) 
         );
         ++_noutstanding_orders;
         /* --- CRITICAL SECTION --- */
    }    
    _order_queue_cond.notify_one();
    
    id_type ret_id;
    try{         
        ret_id = f.get(); /* BLOCKING (on f)*/            
    }catch(...){
        _block_on_outstanding_orders(); /* BLOCKING (on _noutstanding_orders) */         
        _clear_callback_queue(); 
        throw;
    }
        
    _block_on_outstanding_orders(); /* BLOCKING (on _noutstanding_orders) */         
    _clear_callback_queue(); 
    return ret_id;
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order_no_wait( order_type oty, 
                                bool buy, 
                                plevel limit,
                                plevel stop,
                                size_t size,
                                order_exec_cb_type cb,                                       
                                order_admin_cb_type admin_cb,
                                id_type id )
{ 
    {
        std::lock_guard<std::mutex> lock(_order_queue_mtx);
        /* --- CRITICAL SECTION --- */
        _order_queue.push(
            order_queue_elem_type(
                oty, buy, limit, stop, size, cb, id, admin_cb,
                std::move(std::promise<id_type>()) /* dummy promise */
            ) 
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
                throw std::logic_error("_noutstanding_orders < 0");
            }else if( _noutstanding_orders == 0 ){
                break;
            }
            /* --- CRITICAL SECTION --- */
        }
        std::this_thread::yield();
    }
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

    std::deque<dfrd_cb_elem_type> cb_elems;
    {     
        std::lock_guard<std::mutex> lock(_master_mtx); 
        /* --- CRITICAL SECTION --- */    
        std::move( _deferred_callback_queue.begin(),
                   _deferred_callback_queue.end(), 
                   back_inserter(cb_elems) );        
        _deferred_callback_queue.clear(); 
        /* --- CRITICAL SECTION --- */
    }    

    order_exec_cb_type cb;
    for( auto & e : cb_elems ){     
        cb = T_(e,1);
        if( cb ){ 
            cb( T_(e,0), T_(e,2), T_(e,3), T_(e,4) );
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
    plevel limit;         
    size_t sz;
    /*
     * need to copy the relevant chain, delete original, THEN insert
     * if not we can hit the same order more than once / go into infinite loop
     */
    stop_chain_type cchain = stop_chain_type(plev->second);
    plev->second.clear();
    _stop_exec<BuyStops>::adjust_state_after_trigger(this, plev);

    for( auto & e : cchain ){
        limit = (plevel)T_(e.second,1);
        cb = T_(e.second,3);
        sz = T_(e.second,2);
       /*
        * note we are keeping the old id
        * 
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; we simply increment _noutstanding_orders instead
        * and block on that when necessary.
        */    
        if( limit ){ /* stop to limit */        
            if( cb ){ 
                /*** PROTECTED BY _master_mtx ***/
                _deferred_callback_queue.push_back( 
                    dfrd_cb_elem_type(
                        callback_msg::stop_to_limit, 
                        cb, e.first, _itop(limit), sz
                    ) 
                );  
                /*** PROTECTED BY _master_mtx ***/          
            }
            _push_order_no_wait(order_type::limit, T_(e.second,0), limit, 
                                nullptr, sz, cb, nullptr, e.first);     
        }else{ /* stop to market */
            _push_order_no_wait(order_type::market, T_(e.second,0), nullptr, 
                                nullptr, sz, cb, nullptr, e.first);
        }
    }
}


SOB_TEMPLATE
template<bool BuyLimit>
void 
SOB_CLASS::_insert_limit_order( plevel limit,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                id_type id,
                                order_admin_cb_type admin_cb )
{
    size_t rmndr = size; 
    if( (BuyLimit && limit >= _ask) || (!BuyLimit && limit <= _bid) ){
        /* If there are matching orders on the other side fill @ market
               - pass ref to callback functor, we'll copy later if necessary 
               - return what we couldn't fill @ market */
        rmndr = _trade<!BuyLimit>(limit,id,size,exec_cb);
    }
 
    if( rmndr > 0) {
        /* insert what remains as limit order
           copy callback functor, needs to persist */  
        limit_chain_type *orders = &limit->first; 
        orders->insert( 
            limit_chain_type::value_type(
                id, 
                limit_bndl_type(rmndr, exec_cb)
            ) 
        );        
        _limit_exec<BuyLimit>::adjust_state_after_insert(this, limit, orders);         
    }
    
    if( admin_cb ){
        admin_cb(id);
    }
}


SOB_TEMPLATE
template<bool BuyMarket>
void 
SOB_CLASS::_insert_market_order( size_t size,
                                 order_exec_cb_type exec_cb,
                                 id_type id,
                                 order_admin_cb_type admin_cb )
{
    size_t rmndr = _trade<!BuyMarket>(nullptr, id, size, exec_cb);
    if( rmndr > 0 ){
        std::string msg = "market order couldn't fill: (" 
                          + std::to_string(size-rmndr) + "/" 
                          + std::to_string(size) + ") filled";
        throw liquidity_exception(msg.c_str());
    }
      
    if( admin_cb ){ 
        admin_cb(id);
    }
}


SOB_TEMPLATE
template<bool BuyStop>
void 
SOB_CLASS::_insert_stop_order( plevel stop,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               id_type id,
                               order_admin_cb_type admin_cb)
{
    /* use stop_limit overload; nullptr as limit */
    _insert_stop_order<BuyStop>( stop, nullptr, size, std::move(exec_cb), id, 
                                 admin_cb );
}


SOB_TEMPLATE
template<bool BuyStop>
void 
SOB_CLASS::_insert_stop_order( plevel stop,
                               plevel limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               id_type id,
                               order_admin_cb_type admin_cb )
{  
   /*  we need an actual trade @/through the stop, i.e can't assume
       it's already been triggered by where last/bid/ask is...
     
           - simply pass the order to the appropriate stop chain     
           - copy callback functor, needs to persist)                */

    stop_chain_type* orders = &stop->second;
    orders->insert( 
        stop_chain_type::value_type(
            id, 
            stop_bndl_type(BuyStop, (void*)limit, size, exec_cb)
        ) 
    );   
    _stop_exec<BuyStop>::adjust_state_after_insert(this, stop);  
    
    if( admin_cb ){ 
        admin_cb(id);
    }
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy> 
std::map<double, 
         typename std::conditional<Side == side_of_market::both, 
                                   std::pair<size_t, side_of_market>,
                                   size_t>::type >
SOB_CLASS::_market_depth(size_t depth) const
{
    plevel h;
    plevel l;    
    size_t d;
    std::map<double, typename std::conditional<Side == side_of_market::both, 
                                    std::pair<size_t, side_of_market>,
                                    size_t>::type > md;
    
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */      
    _high_low<Side>::template set_using_depth<ChainTy>(this,&h,&l,depth);
    for( ; h >= l; --h){
        if( !h->first.empty() ){
            d = _chain<limit_chain_type>::size(&h->first);   
            auto v = _depth<Side>::build_value(this,h,d);
            md.insert( std::make_pair(_itop(h), v) );          
        }
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
    _high_low<Side>::template set_using_cached<ChainTy>(this,&h,&l);      
    for( ; h >= l; --h){ 
        tot += _chain<ChainTy>::size(_chain<ChainTy>::get(h));
    }
    return tot;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename FirstChainTy, typename SecondChainTy>
order_info_type 
SOB_CLASS::_get_order_info(id_type id) const
{
    _assert_valid_chain<FirstChainTy>();
    _assert_valid_chain<SecondChainTy>();
    
    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */    
    auto pc1 = _chain<FirstChainTy>::find(this,id);
    plevel p = T_(pc1,0);
    FirstChainTy *fc = T_(pc1,1);
    if(!p || !fc){
        auto pc2 = _chain<SecondChainTy>::find(this,id);
        p = T_(pc2,0);
        SecondChainTy *sc = T_(pc2,1);
        return (!p || !sc)
            ? _order_info<void>::generate() /* null version */
            : _order_info<SecondChainTy>::generate(this, id, p, sc); 
    }

    return _order_info<FirstChainTy>::generate(this, id, p, fc);         
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename ChainTy, bool IsLimit>
bool 
SOB_CLASS::_pull_order(id_type id)
{ 
     /*** CALLER MUST HOLD LOCK ON _master_mtx OR RACE CONDTION WITH CALLBACK QUEUE ***/
    auto cp = _chain<ChainTy>::find(this,id);
    plevel p = T_(cp,0);
    ChainTy *c = T_(cp,1);
    if(!c || !p){
        return false;
    }

    /* get the callback and, if stop order, its direction... before erasing */
    auto bndl = c->at(id);
    order_exec_cb_type cb = _get_cb_from_bndl(bndl);
    bool is_buystop = IsLimit ? false : T_(bndl,0); 

    c->erase(id);

    /* adjust cache vals as necessary */
    if(IsLimit && c->empty()){
        /*  we can compare vs bid because if we get here and the order is 
            a buy it must be <= the best bid, otherwise its a sell 

           (remember, p is empty if we get here)  */
        if(p <= _bid){ 
            _limit_exec<true>::adjust_state_after_pull(this, p);
        }else{
            _limit_exec<false>::adjust_state_after_pull(this, p);
        }
    }else if(!IsLimit && is_buystop){        
        if( _stop_exec<true>::stop_chain_is_empty(this, (stop_chain_type*)c) ){
            _stop_exec<true>::adjust_state_after_pull(this, p);
        }
    }else if(!IsLimit && !is_buystop){       
        if( _stop_exec<false>::stop_chain_is_empty(this, (stop_chain_type*)c) ){
            _stop_exec<false>::adjust_state_after_pull(this, p);       
        }
    }
       
    /*** PROTECTED BY _master_mtx ***/    
    _deferred_callback_queue.push_back( /* callback with cancel msg */ 
        dfrd_cb_elem_type( callback_msg::cancel, cb, id, 0, 0 ) 
    );   

    return true;
}


SOB_TEMPLATE
template<bool BuyNotSell>
void 
SOB_CLASS::_dump_limits() const
{ 
    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    plevel h = BuyNotSell ? _bid : _high_sell_limit;
    plevel l = BuyNotSell ? _low_buy_limit : _ask;
    _high_low<>::range_check(this,&h,&l);
    
    for( ; h >= l; --h){    
        if( !h->first.empty() ){
            std::cout<< _itop(h);
            for(const limit_chain_type::value_type& e : h->first){
                std::cout<< " <" << e.second.first << " #" << e.first << "> ";
            }
            std::cout<< std::endl;
        } 
    }
    /* --- CRITICAL SECTION --- */
}

SOB_TEMPLATE
template< bool BuyNotSell >
void SOB_CLASS::_dump_stops() const
{ 
    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    plevel h = BuyNotSell ? _high_buy_stop : _high_sell_stop;
    plevel l = BuyNotSell ? _low_buy_stop : _low_sell_stop;
    _high_low<>::range_check(this,&h,&l);    
    
    plevel plim;
    for( ; h >= l; --h){ 
        if( !h->second.empty() ){
            std::cout<< _itop(h);
            for(const auto & e : h->second){
                plim = (plevel)T_(e.second,1);
                std::cout<< " <" << (T_(e.second,0) ? "B " : "S ")
                                 << std::to_string( T_(e.second,2) ) << " @ "
                                 << (plim ? std::to_string(_itop(plim)) : "MKT")
                                 << " #" << std::to_string(e.first) << "> ";
            }
            std::cout<< std::endl;
        } 
    }
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
typename SOB_CLASS::plevel 
SOB_CLASS::_ptoi(TrimmedRational<TickRatio> price) const
{  /* 
    * the range checks in here are 1 position more restrictive to catch
    * bad user price data passed but allow internal index conversions(_itop)
    * 
    * this means that internally we should not convert to a price when
    * a pointer is past beg / at end, signaling a null value
    * 
    * if this causes trouble just create a seperate user input check
    */   
    size_t incr_offset = round((price - _base) * TickRatio::den/TickRatio::num);
    plevel plev = _beg + incr_offset;
    if(plev < _beg){
        throw std::range_error( "plevel < _beg" );
    }
    if(plev >= _end){
        throw std::range_error( "plevel >= _end" );
    }
    return plev;
}


SOB_TEMPLATE 
TrimmedRational<TickRatio>
SOB_CLASS::_itop(plevel plev) const
{   
    if(plev < _beg - 1){
        throw std::range_error( "plevel < _beg - 1" );
    }
    if(plev > _end ){
        throw std::range_error( "plevel > _end" );
    }
    
    long long offset = plev - _beg;
    double incr_offset = (double)offset * TickRatio::num / TickRatio::den;
    return _base + TrimmedRational<TickRatio>(incr_offset);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_limit_order( bool buy,
                               double limit,
                               size_t size,
                               order_exec_cb_type exec_cb,
                               order_admin_cb_type admin_cb ) 
{      
    if(size == 0){
        throw invalid_order("invalid order size");
    }
    
    plevel plev;
    try{
        plev = _ptoi(TrimmedRational<TickRatio>(limit));    
    }catch(std::range_error){
        throw invalid_order("invalid limit price");
    }        
 
    return _push_order_and_wait(order_type::limit, buy, plev, nullptr, 
                                size, exec_cb, admin_cb);    
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_market_order( bool buy,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                order_admin_cb_type admin_cb )
{    
    if(size == 0){
        throw invalid_order("invalid order size");
    }
    return _push_order_and_wait(order_type::market, buy, nullptr, nullptr, 
                                size, exec_cb, admin_cb); 
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              order_admin_cb_type admin_cb )
{
    return insert_stop_order(buy, stop, 0, size, exec_cb, admin_cb);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              double stop,
                              double limit,
                              size_t size,
                              order_exec_cb_type exec_cb,
                              order_admin_cb_type admin_cb )
{      
    if(size == 0){
        throw invalid_order("invalid order size");
    }

    plevel plimit;
    plevel pstop;
    try{
        plimit = limit ? _ptoi(TrimmedRational<TickRatio>(limit)) : nullptr;
        pstop = _ptoi(TrimmedRational<TickRatio>(stop));         
    }catch(std::range_error){
        throw invalid_order("invalid price");
    }    
    order_type oty = limit ? order_type::stop_limit : order_type::stop;

    return _push_order_and_wait(oty, buy, plimit, pstop, size, exec_cb, admin_cb);    
}


SOB_TEMPLATE
bool 
SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{
    if(id == 0){
        throw invalid_order("invalid order id(0)");
    }
    return _push_order_and_wait(order_type::null, search_limits_first, 
                                nullptr, nullptr, 0, nullptr, nullptr,id); 
}


SOB_TEMPLATE
order_info_type 
SOB_CLASS::get_order_info(id_type id, bool search_limits_first) const
{        
    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */
    return search_limits_first
        ? _get_order_info<limit_chain_type, stop_chain_type>(id)        
        : _get_order_info<stop_chain_type, limit_chain_type>(id);
    /* --- CRITICAL SECTION --- */         
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_limit_order( id_type id,
                                     bool buy,
                                     double limit,
                                     size_t size,
                                     order_exec_cb_type exec_cb,
                                     order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_limit_order(buy, limit, size, exec_cb, admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_market_order( id_type id,
                                      bool buy,
                                      size_t size,
                                      order_exec_cb_type exec_cb,
                                      order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_market_order(buy, size, exec_cb, admin_cb);
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
                                    order_admin_cb_type admin_cb )
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, size, exec_cb, admin_cb);
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
                                    order_admin_cb_type admin_cb)
{
    id_type id_new = 0;    
    if( pull_order(id) ){
        id_new = insert_stop_order(buy, stop, limit, size, exec_cb, admin_cb);
    }    
    return id_new;
}


SOB_TEMPLATE
void 
SOB_CLASS::dump_cached_plevels() const
{
    static auto to_str = [&](plevel p){ return std::to_string(_itop(p)); };
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::cout<< "*** CACHED PLEVELS ***" << std::endl;      
    std::cout<< "_high_sell_limit: " << to_str(_high_sell_limit) << std::endl;
    std::cout<< "_high_buy_stop: "<< to_str(_low_buy_stop) << std::endl;
    std::cout<< "_low_buy_stop: " << to_str(_low_buy_stop) << std::endl;
    std::cout<< "last: " << to_str(_last) << std::endl;
    std::cout<< "_high_sell_stop: " << to_str(_high_sell_stop) << std::endl;
    std::cout<< "_low_sell_stop: " << to_str(_low_sell_stop) << std::endl;
    std::cout<< "_low_buy_limit: " << to_str(_low_buy_limit) << std::endl;
    /* --- CRITICAL SECTION --- */
}


};

#undef T_
#undef SOB_TEMPLATE
#undef SOB_CLASS
