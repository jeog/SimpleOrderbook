/*
Copyright (C) 2015 Jonathon Ogden < jeog.dev@gmail.com >

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

#include "types.hpp"
#include "simpleorderbook.hpp"

#define T_(tuple,i) std::get<i>(tuple)

namespace NativeLayer{

namespace SimpleOrderbook{

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbook(my_price_type price, 
                           my_price_type min, 
                           my_price_type max,
                           int sleep) /*=500 ms*/
    :   
        /*  ORDER OF INITIALIZATION IS IMPORTANT */

        _bid_size(0),
        _ask_size(0),
        _last_size(0), 

        /* RANGE CHECKS
           note: these need to happen before (almost) all other initialization */
        _lower_incr(_incrs_in_range(min,price)),
        _upper_incr(_incrs_in_range(price,max)),
        _total_incr(_generate_and_check_total_incr()),

        _base(min),
        _book(_total_incr + 1), /*pad the beg side */

       /************************************************************************
       :: our ersatz iterator approach ::
         
       i = [ 0, _total_incr ) 
     
       vector iterator  [begin()]                                       [ end() ]
       internal pointer [ _base ][ _beg ]                               [ _end  ]
       internal index   [ NULL  ][   i  ][ i+1 ]...    [ _total_incr-1 ][  NULL ]
       external price   [ THROW ][ min  ]                      [  max  ][ THROW ]        
    
       *************************************************************************/
        _beg( &(*_book.begin()) + 1 ), 
        _end( &(*_book.end())), 
        _last( _beg + _lower_incr ), 
        _bid( &(*(_beg-1)) ),
        _ask( &(*_end) ),

        /* cache range vals for faster lookups */
        _low_buy_limit( &(*_end) ),
        _high_sell_limit( &(*(_beg-1)) ),
        _low_buy_stop( &(*_end) ),
        _high_buy_stop( &(*(_beg-1)) ),
        _low_sell_stop( &(*_end) ),
        _high_sell_stop( &(*(_beg-1)) ),

        _total_volume(0),
        _last_id(0),
        _market_makers(),
        _deferred_callback_queue(),
        _cbs_in_progress(false),
        _t_and_s(),
        _t_and_s_max_sz(1000),
        _t_and_s_full(false),
        _order_queue(),
        _order_queue_mtx(new std::mutex), /* smart ptr, no need t delete */   
        _order_queue_cond(),
        _master_mtx(new std::mutex), /* smart ptr, no need t delete */
        _mm_mtx(new std::recursive_mutex), /* smart ptr, no need t delete */
        _busy_with_callbacks(false),
        _need_check_for_stops(false),
        _master_run_flag(true)
    {             
        if( min.to_incr() == 0 )
            throw std::invalid_argument("(TrimmedRational) min price must be > 0");

        _t_and_s.reserve(_t_and_s_max_sz);         
        /* 
         * --- DONT THROW AFTER THIS POINT --- 
         * 
         *    1) _master_run_flag = true
         *    ...
         *    2) launch new _order_dispatcher 
         *    3) launch new _waker 
         */
        _order_dispatcher_thread = 
            std::thread(std::bind(&SOB_CLASS::_threaded_order_dispatcher,this));        
        
        _waker_thread = 
            std::thread(std::bind(&SOB_CLASS::_threaded_waker,this,sleep));        
 
        std::cout<< "+ SimpleOrderbook Created\n";
    }


SOB_TEMPLATE 
SOB_CLASS::~SimpleOrderbook()
    { /*
       *    1) _master_run_flag = false
       *    2) join killed _waker 
       *    3) join killed _order_dispatcher 
       */
        _master_run_flag = false;
        try{ 
            if(_waker_thread.joinable())
                _waker_thread.join(); 
        }catch(...){
        } 
   
        try{ 
            {
                std::lock_guard<std::mutex> lock(*_order_queue_mtx);
                _order_queue.push(order_queue_elem_type()); 
            }    
            _order_queue_cond.notify_one();

            if(_order_dispatcher_thread.joinable())
                _order_dispatcher_thread.join(); 
        }catch(...){
        }

        std::cout<< "- SimpleOrderbook Destroyed\n";
    }


/*
 * nested static calls used to get around member specialization restrictions
 * 
 * _high_low::range_check : bounds check and reset plevels if necessary
 * _high_low::set_using_depth : populate plevels using passed depth 
 *     from 'inside' bid/ask and internal bounds
 * _high_low::set_using_cached : populate plevels using cached extremes
 * 
 * (note: _high_low specials inherit from non-special to access range_check())
 * 
 * _order_info::generate : generate specialized order_info_type tuples
 * 
 * _chain::get : get appropriate chain from plevel
 * _chain::size : get size of chain
 * _chain::find : find chain containing a particular order id
 * 
 * (note: the _chain specials inherit from non-special to access base find
 * 
 * TODO: replace some of the default beg/ends with cached extre
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
            *pl = (plevel)min((plevel)min(sob->_low_sell_stop, sob->_low_buy_stop),
                              (plevel)min(sob->_high_sell_stop, sob->_high_buy_stop));

            *ph = (plevel)max((plevel)max(sob->_low_sell_stop, sob->_low_buy_stop),
                              (plevel)max(sob->_high_sell_stop, sob->_high_buy_stop)); 
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
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_type depth )
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
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_type depth)
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
    set_using_depth(const My* sob, plevel* ph, plevel* pl, size_type depth)
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
template<typename ChainTy, typename My> 
struct SOB_CLASS::_order_info{
    static inline order_info_type 
    generate()
    { 
        return order_info_type(order_type::null,false,0,0,0);
    }
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
        return order_info_type(order_type::limit,(p < sob->_ask), sob->_itop(p), 0, T_(c->at(id),0));
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
        
        if(stop_limit_plevel){    
            return order_info_type(order_type::stop_limit, T_(bndl,0), sob->_itop(p), 
                                   sob->_itop(stop_limit_plevel), T_(bndl,2));
        }
        
        return order_info_type(order_type::stop, T_(bndl,0), sob->_itop(p), 0, T_(bndl,2));
    }
};


SOB_TEMPLATE
template<typename ChainTy, typename Dummy> 
struct SOB_CLASS::_chain {    
    static inline ChainTy* 
    get(typename SOB_CLASS::plevel p)
    { 
        return nullptr; 
    }

    static inline size_type 
    size(ChainTy* c)
    { 
        return nullptr; 
    }

protected:
    template<typename InnerChainTy, typename My>
    static std::pair<typename SOB_CLASS::plevel,InnerChainTy*> 
    find(const My* sob, id_type id)
    { 
        plevel beg, end;
        InnerChainTy* c;
        _high_low<>::template set_using_cached<InnerChainTy>(sob,&end,&beg); 
       
        for( ; beg <= end; ++beg ){
            c = _chain<InnerChainTy>::get(beg);
            for(auto & e : *c){
                if(e.first == id) 
                    return std::pair<plevel,InnerChainTy*>(beg,c);                     
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
    { 
        return &(p->first); 
    } 

    static size_type 
    size(chain_type* c)
    { 
        size_type sz = 0;
        for(auto & e : *c)
            sz += e.second.first;
        return sz;
    }  
  
    template<typename My>
    static inline std::pair<typename SOB_CLASS::plevel,chain_type*>    
    find(const My* sob, id_type id)
    {        
        return _chain<void>::template find<chain_type>(sob,id); 
    }
};


SOB_TEMPLATE
template<typename Dummy> 
struct SOB_CLASS::_chain<typename SOB_CLASS::stop_chain_type, Dummy>
        : public _chain<void> { 
    typedef typename SOB_CLASS::stop_chain_type chain_type;

    static inline chain_type* 
    get(typename SOB_CLASS::plevel p)
    { 
        return &(p->second); 
    }

    static size_type 
    size(chain_type* c)
    {
        size_type sz = 0;
        for(auto & e : *c)
            sz += T_(e.second,2); 
        return sz;
    }    

    template<typename My>
    static inline std::pair<typename SOB_CLASS::plevel,chain_type*> 
    find(const My* sob, id_type id)
    {        
        return _chain<void>::template find<chain_type>(sob,id); 
    }
};


/*
 *  _lift_offers / _hit_bids : are the guts of order execution:
 *      match limit/market orders against the order book,
 *      adjust internal state,
 *      check for overflows  
 *
 *  _hit_chain : handles all the trades at a particular plevel
 *               returns what it couldn't fill
 *
 *  _find_new_best_ask / _find_new_best_bid : reset the ask / bid plevel                 
 */

SOB_TEMPLATE 
size_type 
SOB_CLASS::_lift_offers( plevel plev, 
                         id_type id, 
                         size_type size,
                         order_exec_cb_type& exec_cb )
{
    while( (plev >= _ask || !plev) 
           && (size > 0)
           && (_ask < _end) )
    {         
        /* see how much we can trade at this level */
        size = _hit_chain(_ask, id, size, exec_cb);     
        
        if( !_find_new_best_ask() )
            break;
    }

    return size; /* what we couldn't fill */
}


SOB_TEMPLATE 
bool 
SOB_CLASS::_find_new_best_ask()
{
    /* if on an empty chain 'jump' to next that isn't, reset _ask as we go */
    for( ; 
         _ask->first.empty() && (_ask < _end); 
         ++_ask ) 
        { 
        }                

    /* reset ask size; if we run out of offers reset state/cache and return */          
    if(_ask >= _end){
        _ask_size = 0;          
        _high_sell_limit = _beg-1;    
        return false;
    }else{
        _ask_size = _chain<limit_chain_type>::size(&_ask->first);    
    }
            
    /* adjust cache */
    if(_ask > _high_sell_limit)
        _high_sell_limit = _ask;

    return true;
}


SOB_TEMPLATE 
size_type 
SOB_CLASS::_hit_bids( plevel plev,
                      id_type id,
                      size_type size,
                      order_exec_cb_type& exec_cb )
{  
    while( (plev <= _bid || !plev) 
           && (size > 0)
           && (_bid >= _beg) )
    {         
        /* see how much we can trade at this level */
        size = _hit_chain(_bid, id, size, exec_cb);  
             
        if( !_find_new_best_bid() )
            break;
    }

    return size; /* what we couldn't fill */
}


SOB_TEMPLATE 
bool 
SOB_CLASS::_find_new_best_bid()
{
    /* if on an empty chain 'jump' to next that isn't, reset _bid as we go */
    for( ; 
         _bid->first.empty() && (_bid >= _beg); 
         --_bid )
       {  
       }            

    /* reset ask size; if we run out of offers reset state/cache and return */                  
    if(_bid < _beg){
        _bid_size = 0;             
        _low_buy_limit = _end; 
        return false;
    }else{
        _bid_size = _chain<limit_chain_type>::size(&_bid->first);     
    }
                
    /* adjust cache */
    if(_bid < _low_buy_limit)
        _low_buy_limit = _bid;

    return true;
}



SOB_TEMPLATE
size_type
SOB_CLASS::_hit_chain( plevel plev,
                       id_type id,
                       size_type size,
                       order_exec_cb_type& exec_cb )
{
    size_type amount;
    long long rmndr;
 
    auto del_iter = plev->first.begin();

    /* check each order, FIFO, for this plevel */
    for(auto & elem : plev->first)
    {        
        amount = std::min(size, elem.second.first);

        /* push callbacks into queue; update state */
        _trade_has_occured(plev, amount, id, elem.first, exec_cb, elem.second.second, true);

        /* reduce the amount left to trade */ 
        size -= amount;    
        rmndr = elem.second.first - amount;
        if(rmndr > 0) 
            elem.second.first = rmndr; /* adjust outstanding order size */
        else                    
            ++del_iter; /* indicate removal if we cleared bid */   
     
        if(size <= 0) 
            break; /* if we have nothing left to trade*/
    }
    plev->first.erase(plev->first.begin(),del_iter);  

    return size;
}


SOB_TEMPLATE
void 
SOB_CLASS::_trade_has_occured( plevel plev,
                               size_type size,
                               id_type idbuy,
                               id_type idsell,
                               order_exec_cb_type& cbbuy,
                               order_exec_cb_type& cbsell,
                               bool took_offer )
{  /*
    * CAREFUL: we can't insert orders from here since we have yet to finish
    * processing the initial order (possible infinite loop);
    */  
    price_type p = _itop(plev);
    
    _deferred_callback_queue.push_back( /* buy side */
        dfrd_cb_elem_type(
            callback_msg::fill,     
            cbbuy, 
            idbuy, 
            p, 
            size
        )
    );

    _deferred_callback_queue.push_back( /* sell side */
        dfrd_cb_elem_type(
            callback_msg::fill, 
            cbsell, 
            idsell, 
            p, 
            size
        )
    );
    
    if(_t_and_s_full)
        _t_and_s.pop_back();
    else if( _t_and_s.size() >= (_t_and_s_max_sz - 1) )
        _t_and_s_full = true;

    _t_and_s.push_back( t_and_s_type(clock_type::now(),p,size) );

    _last = plev;
    _total_volume += size;
    _last_size = size;
    _need_check_for_stops = true;
}


SOB_TEMPLATE
void 
SOB_CLASS::_threaded_waker(int sleep)
{
    if(sleep <= 0) 
        return;
    else if(sleep < 100)
        std::cerr<< "sleep < 100ms in _threaded_waker; consider larger value\n";
    
    while(_master_run_flag){ 
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
        std::lock_guard<std::recursive_mutex> lock(*_mm_mtx);
        /* ---(OUTER) CRITICAL SECTION --- */ 
        for(auto & mm : _market_makers)
        {    
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
            std::lock_guard<std::mutex> lock(*_master_mtx);
            /* ---(INNER) CRITICAL SECTION --- */                
            _deferred_callback_queue.push_back( /* callback with wake msg */    
                dfrd_cb_elem_type(
                    callback_msg::wake, 
                    mm->get_callback(), 
                    0, 
                    _itop(_last), 
                    0
                ) 
            ); 
            /* ---(INNER) CRITICAL SECTION --- */
        }
        /* ---(OUTER) CRITICAL SECTION --- */ 
    }
}


SOB_TEMPLATE
void 
SOB_CLASS::_threaded_order_dispatcher()
{    
    order_queue_elem_type e;
    std::promise<id_type> p;    
    id_type id;    
    
    for( ; ; ){
        {
            std::unique_lock<std::mutex> lock(*_order_queue_mtx);      
            _order_queue_cond.wait( 
                lock, 
                [this]{ return !this->_order_queue.empty(); }
            );
 
            if(!_master_run_flag)
                break;

            e = std::move(_order_queue.front());
            _order_queue.pop();
        }         
        
        p = std::move( T_(e,8) );        
        id = T_(e,6);
        if(!id) 
            id = _generate_id();
        
        try{
            _route_order(e,id);
        }catch(...){          
            p.set_exception( std::current_exception() );
            continue;
        }
     
        p.set_value(id);    
    }    
}


SOB_TEMPLATE
void 
SOB_CLASS::_route_order(order_queue_elem_type& e, id_type& id)
{
    std::lock_guard<std::mutex> lock(*_master_mtx); 
    /* --- CRITICAL SECTION --- */
    try{
        switch( T_(e,0) ){            
        case order_type::limit:                
            _insert_limit_order(T_(e,1), T_(e,2), T_(e,4), T_(e,5), id, T_(e,7));             
            _look_for_triggered_stops(false); /* throw */      
            break;
       
        case order_type::market:                
            _insert_market_order(T_(e,1), T_(e,4), T_(e,5), id, T_(e,7));              
            _look_for_triggered_stops(false); /* throw */               
            break;
      
        case order_type::stop:        
            _insert_stop_order(T_(e,1), T_(e,3), T_(e,4), T_(e,5), id, T_(e,7));
            break;
         
        case order_type::stop_limit:        
            _insert_stop_order(T_(e,1), T_(e,3), T_(e,2), T_(e,4), T_(e,5), id, T_(e,7));                 
            break;
         
        case order_type::null: /* not the cleanest but most effective/thread-safe */       
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
                                 size_type size,
                                 order_exec_cb_type cb,                                              
                                 order_admin_cb_type admin_cb,
                                 id_type id )
{
    id_type ret_id;
    std::promise<id_type> p;
    std::future<id_type> f(p.get_future());    
    {
         std::lock_guard<std::mutex> lock(*_order_queue_mtx);
         _order_queue.push(
             order_queue_elem_type(oty, buy, limit, stop, size, cb, id, 
                                   admin_cb, std::move(p)) );
    }    
    _order_queue_cond.notify_one();
    
    try{         
        ret_id = f.get(); /* BLOCKING */            
    }catch(...){
        _clear_callback_queue();        
        throw;
    }
        
    _clear_callback_queue();    
    return ret_id;
}


SOB_TEMPLATE
void 
SOB_CLASS::_push_order_no_wait( order_type oty, 
                                bool buy, 
                                plevel limit,
                                plevel stop,
                                size_type size,
                                order_exec_cb_type cb,                                       
                                order_admin_cb_type admin_cb,
                                id_type id )
{ 
    {
        std::lock_guard<std::mutex> lock(*_order_queue_mtx);
        _order_queue.push(
            order_queue_elem_type(
                oty, buy, limit, stop, 
                size, cb, id, admin_cb,
/* dummy --> */ std::move(std::promise<id_type>())
            ) 
        );
    }    
    _order_queue_cond.notify_one();
}


SOB_TEMPLATE
void 
SOB_CLASS::_clear_callback_queue()
{
    order_exec_cb_type cb;
    std::deque<dfrd_cb_elem_type> cb_elems;
    
    bool busy = false; 
    /* if false, set to true(atomically); if true return */   
    _busy_with_callbacks.compare_exchange_strong(busy,true);
    if(busy) 
        return;    

    {     
        std::lock_guard<std::mutex> lock(*_master_mtx); 
        /* --- CRITICAL SECTION --- */    
        std::move( _deferred_callback_queue.begin(),
                   _deferred_callback_queue.end(), 
                   back_inserter(cb_elems) );    
     
        _deferred_callback_queue.clear(); 
        /* --- CRITICAL SECTION --- */
    }    

    for(auto & e : cb_elems){     
        cb = T_(e,1);
        if(cb) 
            cb(T_(e,0), T_(e,2), T_(e,3), T_(e,4));                
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
        if(!_need_check_for_stops)
            return;

        _need_check_for_stops = false;

        for(plevel low = _low_buy_stop; low <= _last; ++low)  
        {      
            _handle_triggered_stop_chain<true>(low); 
        }

        for(plevel high = _high_sell_stop; high >= _last; --high)
        {
            _handle_triggered_stop_chain<false>(high);    
        }

    }catch(...){
        if(!nothrow)
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
    stop_chain_type cchain;
    order_exec_cb_type cb;
    plevel limit;         
    size_type sz;
    /*
     * need to copy the relevant chain, delete original, THEN insert
     * if not we can hit the same order more than once / go into infinite loop
     */
    cchain = stop_chain_type(plev->second);
    plev->second.clear();

    if(BuyStops){
        _low_buy_stop = plev + 1;        
        /* no more buy stops */ 
        if(_low_buy_stop > _high_buy_stop){            
            _low_buy_stop = _end;
            _high_buy_stop = _beg-1;
        }
    }else{
        _high_sell_stop = plev - 1;
        /* no more sell stops */
        if(_high_sell_stop < _low_sell_stop){            
            _high_sell_stop = _beg-1;
            _low_sell_stop = _end;
        }
    }

    for(auto & e : cchain)
    {
        limit = (plevel)T_(e.second,1);
        cb = T_(e.second,3);
        sz = T_(e.second,2);
       /*
        * note we are keeping the old id
        * 
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; since we can't guarantee an exec before clearing
        * the queue, we don't (NOT A BIG DEAL IN PRACTICE; NEED TO FIX EVENTUALLY)
        */    
        if(limit){ /* stop to limit */        
            if(cb){ 
                _deferred_callback_queue.push_back( 
                    dfrd_cb_elem_type(
                        callback_msg::stop_to_limit, 
                        cb, 
                        e.first, 
                        _itop(limit), 
                        sz
                    ) 
                );            
            }
            _push_order_no_wait(order_type::limit, T_(e.second,0), limit, nullptr, 
                                sz, cb, nullptr, e.first);   
                  
        }else{ /* stop to market */
            _push_order_no_wait(order_type::market, T_(e.second,0), nullptr, 
                                nullptr, sz, cb, nullptr, e.first);
        }
    }
}


SOB_TEMPLATE
void 
SOB_CLASS::_insert_limit_order( bool buy,
                                plevel limit,
                                size_type size,
                                order_exec_cb_type exec_cb,
                                id_type id,
                                order_admin_cb_type admin_cb )
{
    size_type rmndr = size; 
    /*
     * first look if there are matching orders on the offer side
     * pass ref to callback functor, we'll copy later if necessary
     */
    if(buy && limit >= _ask)
        rmndr = _lift_offers(limit,id,size,exec_cb);
    else if(!buy && limit <= _bid)
        rmndr = _hit_bids(limit,id,size,exec_cb);

    /*
     * then add what remains to bid side; copy callback functor, needs to persist
     */
    if(rmndr > 0){
        limit_chain_type* orders = &limit->first;
        limit_bndl_type bndl = limit_bndl_type(rmndr,exec_cb);
        orders->insert(limit_chain_type::value_type(id,std::move(bndl)));
        
        if(buy){
            if(limit >= _bid){
                _bid = limit;
                _bid_size = _chain<limit_chain_type>::size(orders);
            }

            if(limit < _low_buy_limit)
                _low_buy_limit = limit;
        }else{
            if(limit <= _ask){
                _ask = limit;
                _ask_size = _chain<limit_chain_type>::size(orders);
            }

            if(limit > _high_sell_limit)
                _high_sell_limit = limit;
        }
    }
    
    if(admin_cb)
        admin_cb(id);
}


SOB_TEMPLATE
void 
SOB_CLASS::_insert_market_order( bool buy,
                                 size_type size,
                                 order_exec_cb_type exec_cb,
                                 id_type id,
                                 order_admin_cb_type admin_cb )
{
    size_type rmndr = size;

    rmndr = buy ? _lift_offers(nullptr,id,size,exec_cb)
                : _hit_bids(nullptr,id,size,exec_cb);

    if(rmndr == size){        
        throw liquidity_exception("market order couldn't fill any");
    }else if(rmndr > 0){
        std::string msg;
        msg.append("market order couldn't fill all (")
           .append( std::to_string(size-rmndr) )
           .append("/")
           .append( std::to_string(size) )
           .append(")");
        throw liquidity_exception(msg.c_str());
    }
      
    if(admin_cb) 
        admin_cb(id);
}


SOB_TEMPLATE
void 
SOB_CLASS::_insert_stop_order( bool buy,
                               plevel stop,
                               size_type size,
                               order_exec_cb_type exec_cb,
                               id_type id,
                               order_admin_cb_type admin_cb)
{
    _insert_stop_order(buy,stop,nullptr,size,std::move(exec_cb),id,admin_cb);
}


SOB_TEMPLATE
void 
SOB_CLASS::_insert_stop_order( bool buy,
                               plevel stop,
                               plevel limit,
                               size_type size,
                               order_exec_cb_type exec_cb,
                               id_type id,
                               order_admin_cb_type admin_cb )
{  /*
    * we need an actual trade @/through the stop, i.e can't assume
    * it's already been triggered by where last/bid/ask is...
    *
    * simply pass the order to the appropriate stop chain
    *
    * copy callback functor, needs to persist
    */
    stop_chain_type* orders = &stop->second;
    stop_bndl_type bndl = stop_bndl_type(buy,(void*)limit,size,exec_cb);
    orders->insert(stop_chain_type::value_type(id,std::move(bndl)));
    
    /* udpate cache vals */
    if(buy){
        if(stop < _low_buy_stop)    
            _low_buy_stop = stop;
        if(stop > _high_buy_stop) 
            _high_buy_stop = stop;
    }else{ 
        if(stop > _high_sell_stop) 
            _high_sell_stop = stop;
        if(stop < _low_sell_stop)    
            _low_sell_stop = stop;
    }
    
    if(admin_cb) 
        admin_cb(id);
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy> 
typename SOB_CLASS::market_depth_type 
SOB_CLASS::_market_depth(size_type depth) const
{
    plevel h,l;
    market_depth_type md;
    size_type d;
    
    std::lock_guard<std::mutex> lock(*_master_mtx);
    /* --- CRITICAL SECTION --- */ 
    _high_low<Side>::template set_using_depth<ChainTy>(this,&h,&l,depth);    
    for( ; h >= l; --h){
        if( !h->first.empty() ){
            d = _chain<limit_chain_type>::size(&h->first);
            md.insert( market_depth_type::value_type(_itop(h),d) );
        }
    }
    return md;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<side_of_market Side, typename ChainTy> 
size_type 
SOB_CLASS::_total_depth() const
{
    plevel h,l;
    size_type tot;
    
    std::lock_guard<std::mutex> lock(*_master_mtx);
    /* --- CRITICAL SECTION --- */    
    _high_low<Side>::template set_using_cached<ChainTy>(this,&h,&l);    
    tot = 0;
    for( ; h >= l; --h) 
        tot += _chain<ChainTy>::size(_chain<ChainTy>::get(h));
        
    return tot;
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename FirstChainTy, typename SecondChainTy>
order_info_type 
SOB_CLASS::_get_order_info(id_type id) 
{
    plevel p;
    FirstChainTy* fc;
    SecondChainTy* sc;
    
    ASSERT_VALID_CHAIN(FirstChainTy);
    ASSERT_VALID_CHAIN(SecondChainTy);
    
    std::lock_guard<std::mutex> lock(*_master_mtx); 
    /* --- CRITICAL SECTION --- */    
    auto pc1 = _chain<FirstChainTy>::find(this,id);
    p = T_(pc1,0);
    fc = T_(pc1,1);

    if(!p || !fc){
        auto pc2 = _chain<SecondChainTy>::find(this,id);
        p = T_(pc2,0);
        sc = T_(pc2,1);
        return (!p || !sc)
            ? _order_info<void>::generate() /* null version */
            : _order_info<SecondChainTy>::generate(this, id, p, sc); 
    }

    return _order_info<FirstChainTy>::generate(this, id, p, fc);         
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
template<typename ChainTy>
bool 
SOB_CLASS::_pull_order(id_type id)
{ 
    plevel p; 
    order_exec_cb_type cb;
    ChainTy* c;    
    bool is_buystop;

    constexpr bool IsLimit = SAME_(ChainTy,limit_chain_type);

    auto cp = _chain<ChainTy>::find(this,id);
    p = T_(cp,0);
    c = T_(cp,1);

    if(!c || !p)
        return false;   

    /* get the callback and, if stop order, its direction... before erasing */
    auto bndl = c->at(id);
    cb = _get_cb_from_bndl(bndl); 

    if(!IsLimit) 
        is_buystop = T_(bndl,0); 

    c->erase(id);

    /* adjust cache vals as necessary */
    if(IsLimit && c->empty())
        _adjust_limit_cache_vals(p);
    else if(!IsLimit && is_buystop)         
        _adjust_stop_cache_vals<true>(p,(stop_chain_type*)c);
    else if(!IsLimit && !is_buystop)
        _adjust_stop_cache_vals<false>(p,(stop_chain_type*)c); 
        
    /* callback with cancel msg */     
    _deferred_callback_queue.push_back( 
        dfrd_cb_elem_type(
            callback_msg::cancel, 
            cb, 
            id, 
            0, 
            0
        ) 
    ); 

    return true;
}


SOB_TEMPLATE
void 
SOB_CLASS::_adjust_limit_cache_vals(plevel plev)
{    
   /*  we can compare vs bid because if we get here and the order is a buy it
       must be <= the best bid, otherwise its a sell 

       remember, plevel is empty if we get here 
    */
    if(plev <= _bid){ 

        if(plev < _low_buy_limit) /* this *should* never happen */
            throw cache_value_error("can't remove limit lower than cached val");

        if(plev == _low_buy_limit)
            ++_low_buy_limit;  /*dont look for next valid plevel*/

        if(plev == _bid)
            _find_new_best_bid(); 
    }else{

        if(plev > _high_sell_limit) /* this *should* never happen */
            throw cache_value_error("can't remove limit higher than cached val");

        if(plev == _high_sell_limit)
            --_high_sell_limit;  /*dont look for next valid plevel*/

        if(plev == _ask)
            _find_new_best_ask();
    }   
}


SOB_TEMPLATE
template<bool BuyStop>
void 
SOB_CLASS::_adjust_stop_cache_vals(plevel plev,stop_chain_type* c)
{ 
    auto ifcond = [](const stop_chain_type::value_type& v)
                    {
                        return T_(v.second,0) == BuyStop;
                    };

    auto biter = c->cbegin();
    auto eiter = c->cend();
    auto riter = find_if(biter, eiter, ifcond);

    if(riter != eiter)
        return;
    
    if(BuyStop){

        if(plev > _high_buy_stop) /* this *should* never happen */
            throw cache_value_error("can't remove stop higher than cached val");
        else if(plev == _high_buy_stop)
            --_high_buy_stop; /*dont look for next valid plevel*/ 
        
        if(plev < _low_buy_stop) /* this *should* never happen */
            throw cache_value_error("can't remove stop lower than cached val");
        else if(plev == _low_buy_stop)
            ++_low_buy_stop; /*dont look for next valid plevel*/ 
    
    }else{

        if(plev > _high_sell_stop) /* this *should* never happen */
            throw cache_value_error("can't remove stop higher than cached val");
        else if(plev == _high_sell_stop)
            --_high_sell_stop; /*dont look for next valid plevel */     
        
        if(plev < _low_sell_stop) /* this *should* never happen */
            throw cache_value_error("can't remove stop lower than cached val");
        else if(plev == _low_sell_stop)
            ++_low_sell_stop; /*dont look for next valid plevel*/             
    }                
}


SOB_TEMPLATE
template<bool BuyNotSell>
void 
SOB_CLASS::_dump_limits() const
{ 
    plevel h,l;

    std::lock_guard<std::mutex> lock(*_master_mtx); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    h = BuyNotSell ? _bid : _high_sell_limit;
    l = BuyNotSell ? _low_buy_limit : _ask;

    _high_low<>::range_check(this,&h,&l);
    for( ; h >= l; --h){    
        if( !h->first.empty() ){
            std::cout<< _itop(h);
            for(const limit_chain_type::value_type& e : h->first)
                std::cout<< " <" << e.second.first << " #" << e.first << "> ";
            std::cout<< std::endl;
        } 
    }
    /* --- CRITICAL SECTION --- */
}

SOB_TEMPLATE
template< bool BuyNotSell >
void SOB_CLASS::_dump_stops() const
{ 
    plevel h, l, plim;

    std::lock_guard<std::mutex> lock(*_master_mtx); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    h = BuyNotSell ? _high_buy_stop : _high_sell_stop;
    l = BuyNotSell ? _low_buy_stop : _low_sell_stop;

    _high_low<>::range_check(this,&h,&l);    
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
SOB_CLASS::_ptoi(my_price_type price) const
{  /* 
    * the range checks in here are 1 position more restrictive to catch
    * bad user price data passed but allow internal index conversions(_itop)
    * 
    * this means that internally we should not convert to a price when
    * a pointer is past beg / at end, signaling a null value
    * 
    * if this causes trouble just create a seperate user input check
    */
    plevel plev;
    size_type incr_offset;

    incr_offset = round((price - _base) * tick_ratio::den/tick_ratio::num);
    plev = _beg + incr_offset;

    if(plev < _beg)
        throw std::range_error( "chain_pair_type* < _beg" );

    if(plev >= _end)
        throw std::range_error( "plevel >= _end" );

    return plev;
}


SOB_TEMPLATE 
typename SOB_CLASS::my_price_type 
SOB_CLASS::_itop(plevel plev) const
{
    price_type incr_offset;
    long long offset;

    if(plev < _beg - 1)
        throw std::range_error( "plevel < _beg - 1" );

    if(plev > _end )
        throw std::range_error( "plevel > _end" );

    offset = plev - _beg;
    incr_offset = (double)offset * tick_ratio::num / tick_ratio::den;

    return _base + my_price_type(incr_offset);
}


SOB_TEMPLATE
size_type 
SOB_CLASS::_incrs_in_range(my_price_type lprice, my_price_type hprice)
{
    my_price_type h( _round_to_incr(hprice) );
    my_price_type l( _round_to_incr(lprice) );
    size_type i = round((double)(h-l)*tick_ratio::den/tick_ratio::num);
    
    if(lprice < 0 || hprice < 0)
        throw invalid_parameters("price/min/max values can not be < 0"); 
    
    if(i < 0)
        throw invalid_parameters("invalid price/min/max price value(s)");
        
    return (size_type)i;
}


SOB_TEMPLATE
size_type 
SOB_CLASS::_generate_and_check_total_incr()
{
    size_type i;
    
    if(_lower_incr < 1 || _upper_incr < 1)    
        throw invalid_parameters("parameters don't generate enough increments"); 
    
    i = _lower_incr + _upper_incr + 1;
    
    if(i > max_ticks)
        throw allocation_error("tick range requested would exceed MaxMemory");
    
    return i;
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_makers(market_makers_type&& mms)
{
    std::lock_guard<std::recursive_mutex> lock(*_mm_mtx);
    /* --- CRITICAL SECTION --- */                
    for(auto & mm : mms){     
        mm->start(this, _itop(_last), tick_size);        
        _market_makers.push_back(std::move(mm));        
    }
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_maker(MarketMaker&& mm)
{
    std::lock_guard<std::recursive_mutex> lock(*_mm_mtx);
    /* --- CRITICAL SECTION --- */ 
    pMarketMaker pmm = mm._move_to_new();
    add_market_maker(std::move(pmm));
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_maker(pMarketMaker&& mm)
{
    std::lock_guard<std::recursive_mutex> lock(*_mm_mtx);
    /* --- CRITICAL SECTION --- */ 
    mm->start(this, _itop(_last), tick_size);
    _market_makers.push_back(std::move(mm)); 
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_limit_order( bool buy,
                               price_type limit,
                               size_type size,
                               order_exec_cb_type exec_cb,
                               order_admin_cb_type admin_cb ) 
{
    plevel plev;
    
    if(size <= 0)
        throw invalid_order("invalid order size");    
 
    try{
        plev = _ptoi(limit);    
    }catch(std::range_error){
        throw invalid_order("invalid limit price");
    }        
 
    return _push_order_and_wait(order_type::limit, buy, plev, nullptr, 
                                size, exec_cb, admin_cb);    
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_market_order( bool buy,
                                size_type size,
                                order_exec_cb_type exec_cb,
                                order_admin_cb_type admin_cb )
{    
    if(size <= 0)
        throw invalid_order("invalid order size");

    return _push_order_and_wait(order_type::market, buy, nullptr, nullptr, 
                                size, exec_cb, admin_cb); 
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              price_type stop,
                              size_type size,
                              order_exec_cb_type exec_cb,
                              order_admin_cb_type admin_cb )
{
    return insert_stop_order(buy,stop,0,size,exec_cb,admin_cb);
}


SOB_TEMPLATE
id_type 
SOB_CLASS::insert_stop_order( bool buy,
                              price_type stop,
                              price_type limit,
                              size_type size,
                              order_exec_cb_type exec_cb,
                              order_admin_cb_type admin_cb )
{
    plevel plimit, pstop;
    order_type oty;

    if(size <= 0)
        throw invalid_order("invalid order size");

    try{
        plimit = limit ? _ptoi(limit) : nullptr;
        pstop = _ptoi(stop);         
    }catch(std::range_error){
        throw invalid_order("invalid price");
    }    
    oty = limit ? order_type::stop_limit : order_type::stop;

    return _push_order_and_wait(oty,buy,plimit,pstop,size,exec_cb,admin_cb);    
}


SOB_TEMPLATE
bool 
SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{
    return _push_order_and_wait(order_type::null, search_limits_first, 
                                nullptr, nullptr, 0, nullptr, nullptr,id); 
}


SOB_TEMPLATE
order_info_type 
SOB_CLASS::get_order_info(id_type id, bool search_limits_first) 
{     
    {
        std::lock_guard<std::mutex> lock(*_master_mtx); 
        /* --- CRITICAL SECTION --- */
        return search_limits_first
            ? _get_order_info<limit_chain_type, stop_chain_type>(id)        
            : _get_order_info<stop_chain_type, limit_chain_type>(id);
        /* --- CRITICAL SECTION --- */
    }     
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_limit_order( id_type id,
                                     bool buy,
                                     price_type limit,
                                     size_type size,
                                     order_exec_cb_type exec_cb,
                                     order_admin_cb_type admin_cb )
{
    id_type id_new = 0;
    
    if(pull_order(id))
        id_new = insert_limit_order(buy,limit,size,exec_cb,admin_cb);
    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_market_order( id_type id,
                                      bool buy,
                                      size_type size,
                                      order_exec_cb_type exec_cb,
                                      order_admin_cb_type admin_cb )
{
    id_type id_new = 0;
    
    if(pull_order(id))
        id_new = insert_market_order(buy,size,exec_cb,admin_cb);
    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    price_type stop,
                                    size_type size,
                                    order_exec_cb_type exec_cb,
                                    order_admin_cb_type admin_cb )
{
    id_type id_new = 0;
    
    if(pull_order(id))
        id_new = insert_stop_order(buy,stop,size,exec_cb,admin_cb);
    
    return id_new;
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_stop_order( id_type id,
                                    bool buy,
                                    price_type stop,
                                    price_type limit,
                                    size_type size,
                                    order_exec_cb_type exec_cb,
                                    order_admin_cb_type admin_cb)
{
    id_type id_new = 0;
    
    if(pull_order(id))
        id_new = insert_stop_order(buy,stop,limit,size,exec_cb,admin_cb);
    
    return id_new;
}


SOB_TEMPLATE
void 
SOB_CLASS::dump_cached_plevels() const
{
    std::lock_guard<std::mutex> lock(*_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::cout<< "CACHED PLEVELS" << std::endl;       

    std::cout<< "_high_sell_limit: "
             << std::to_string(_itop(_high_sell_limit)) 
             << std::endl;

    std::cout<< "_high_buy_stop: "
             << std::to_string(_itop(_low_buy_stop)) 
             << std::endl;

    std::cout<< "_low_buy_stop: "
             << std::to_string(_itop(_low_buy_stop)) 
             << std::endl;

    std::cout<< "LAST: "
             << std::to_string(_itop(_last)) 
             << std::endl;

    std::cout<< "_high_sell_stop: "
             << std::to_string(_itop(_high_sell_stop)) 
             << std::endl;

    std::cout<< "_low_sell_stop: "
             << std::to_string(_itop(_low_sell_stop)) 
             << std::endl;

    std::cout<< "_low_buy_limit: "
             << std::to_string(_itop(_low_buy_limit)) 
             << std::endl;
    /* --- CRITICAL SECTION --- */
}

};
};
