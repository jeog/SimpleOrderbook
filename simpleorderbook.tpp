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

namespace NativeLayer{

namespace SimpleOrderbook{

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbook(my_price_type price, 
                           my_price_type min, 
                           my_price_type max,
                           int sleep) /*=500 ms*/
    :
        _bid_size(0),
        _ask_size(0),
        _last_size(0), 
        /*** range checks ***/
        _lower_incr(this->_incrs_in_range(min,price)),
        _upper_incr(this->_incrs_in_range(price,max)),
        _total_incr(this->_generate_and_check_total_incr()),
        /*** range checks ***/
        _base( min ),
        _book( _total_incr + 1), /*pad the beg side */
        _beg( &(*_book.begin()) + 1 ), /**/
        _end( &(*_book.end())), 
        _last( this->_beg + _lower_incr ), 
        _bid( &(*(this->_beg-1)) ),
        _ask( &(*this->_end) ),
/*******************************************************************************
 :: our ersatz iterator approach ::
        
    i = [ 0, _total_incr ) 
     
    vector iterator:  [begin()]                                        [ end() ]
    internal pointer: [ _base ][ _beg ]                                [ _end  ]
    internal index:   [ NULL  ][   i  ][ i+1 ]...     [ _total_incr-1 ][  NULL ]
    external price:   [ THROW ][ min  ]                       [  max  ][ THROW ]        
    
*******************************************************************************/
        _low_buy_limit( &(*this->_last) ),
        _high_sell_limit( &(*this->_last) ),
        _low_buy_stop( &(*this->_end) ),
        _high_buy_stop( &(*(this->_beg-1)) ),
        _low_sell_stop( &(*this->_end) ),
        _high_sell_stop( &(*(this->_beg-1)) ),
        _total_volume(0),
        _last_id(0),
        _market_makers(),
        _deferred_callback_queue(),
        _cbs_in_progress(false),
        _t_and_s(),
        _t_and_s_max_sz(1000),
        _t_and_s_full(false),
        _order_queue(),
        _order_queue_mtx(new std::mutex),    
        _order_queue_cond(),
        _master_mtx(new std::mutex),
        _mm_mtx(new std::recursive_mutex),
        _busy_with_callbacks(false),
        _master_run_flag(true)
    {             
        if( min.to_incr() == 0 )
            throw std::invalid_argument("(TrimmedRational) min price must be > 0");

        this->_t_and_s.reserve(this->_t_and_s_max_sz);         
        /* 
         * --- DONT THROW AFTER THIS POINT --- 
         * 
         *    1) _master_run_flag = true
         *    ...
         *    2) launch new _order_dispatcher 
         *    3) launch new _waker 
         */
        this->_order_dispatcher_thread = 
            std::thread(std::bind(&SOB_CLASS::_threaded_order_dispatcher,this));        
        
        this->_waker_thread = 
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
        this->_master_run_flag = false;
        try{ 
            if(this->_waker_thread.joinable())
                this->_waker_thread.join(); 
        }catch(...){
        } 
   
        try{ 
            {
                std::lock_guard<std::mutex> lock(*(this->_order_queue_mtx));
                this->_order_queue.push(order_queue_elem_type()); 
            }    
            this->_order_queue_cond.notify_one();

            if(this->_order_dispatcher_thread.joinable())
                this->_order_dispatcher_thread.join(); 

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
        {
            *pl = sob->_low_buy_limit;
            *ph = sob->_high_sell_limit; 
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
        return order_info_type( order_type::limit,(p < sob->_ask), sob->_itop(p), 
                                0, std::get<0>(c->at(id)) );
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
        typename SOB_CLASS::stop_bndl_type bndl = c->at(id);
        plevel stop_limit_plevel = (plevel)std::get<1>(bndl);
        
        return stop_limit_plevel    
            ? order_info_type(order_type::stop_limit, std::get<0>(bndl), 
                              sob->_itop(p), sob->_itop(stop_limit_plevel), 
                              std::get<2>(bndl))
            : order_info_type(order_type::stop, std::get<0>(bndl), 
                              sob->_itop(p), 0, std::get<2>(bndl));
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
            for(typename InnerChainTy::value_type& e : *c)
                if(e.first == id) 
                    return std::pair<plevel,InnerChainTy*>(beg,c);                     
        }      
  
        return std::pair<typename SOB_CLASS::plevel,InnerChainTy*> (nullptr,nullptr);
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
        for(chain_type::value_type& e : *c)
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
        for(chain_type::value_type& e : *c)
            sz += std::get<2>(e.second); 
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
 *  _lift_offers / _hit_bids are the guts of order execution:
 *      match limit/market orders against the order book,
 *      adjust internal state,
 *      check for overflows                        
 */

SOB_TEMPLATE 
size_type 
SOB_CLASS::_lift_offers( plevel plev, 
                         id_type id, 
                         size_type size,
                         order_exec_cb_type& exec_cb )
{
    limit_chain_type::iterator del_iter;
    size_type amount;
    long long rmndr;
    plevel inside;
 
    inside = this->_ask;
    while( (inside <= plev || !plev) && size > 0 && (inside < this->_end) )
    {         
        /* see how much we can trade at this level */
        size = _hit_chain(inside, id, size, exec_cb);     
        
        /* if on an empty chain 'jump' to one that isn't */
        for( ; 
             inside->first.empty() && inside < this->_end; 
             ++inside) 
            { 
            }                

        /* reset the inside ask */
        this->_ask = inside;      

        /* reset ask size; if we ran out of offers STOP */          
        if(inside >= this->_end){
            this->_ask_size = 0;        
            break;
        }else{
            this->_ask_size = _chain<limit_chain_type>::size(&this->_ask->first);    
        }
                
        /* adjust cached val */
        if(this->_ask > this->_high_sell_limit)
            this->_high_sell_limit = this->_ask;
    }
    return size; /* what we couldn't fill */
}


SOB_TEMPLATE 
size_type 
SOB_CLASS::_hit_bids( plevel plev,
                      id_type id,
                      size_type size,
                      order_exec_cb_type& exec_cb )
{
    limit_chain_type::iterator del_iter;
    size_type amount;
    long long rmndr;
    plevel inside;
    
    inside = this->_bid;
    while( (inside >= plev || !plev) && size > 0 && (inside >= this->_beg) )
    {         
        /* see how much we can trade at this level */
        size = _hit_chain(inside, id, size, exec_cb);  
             
        /* if on an empty chain 'jump' to one that isn't */
        for( ; 
             inside->first.empty() && inside >= this->_beg; 
             --inside) 
           {  
           }            

        /* reset the inside bid */
        this->_bid = inside;      

        /* reset bid size; if we ran out of bids STOP */              
        if(inside < this->_beg){
            this->_bid_size = 0;        
            break;
        }else{
            this->_bid_size = _chain<limit_chain_type>::size(&this->_bid->first);     
        }
                    
        /* adjust cached val */
        if(this->_bid < this->_low_buy_limit)
            this->_low_buy_limit = this->_bid;
    }
    return size; /* what we couldn't fill */
}


SOB_TEMPLATE
size_type
SOB_CLASS::_hit_chain( plevel inside,
                         id_type id,
                         size_type size,
                         order_exec_cb_type& exec_cb )
{
    size_type amount;
    long long rmndr;
 
    limit_chain_type::iterator del_iter = inside->first.begin();

    for(limit_chain_type::value_type& elem : inside->first)
    {
        /* check each order, FIFO, for that price level */
        amount = std::min(size, elem.second.first);
        this->_trade_has_occured(inside, amount, id, elem.first, exec_cb, 
                                 elem.second.second, true);
        size -= amount; /* reduce the amount left to trade */  
  
        rmndr = elem.second.first - amount;
        if(rmndr > 0) 
            elem.second.first = rmndr; /* adjust outstanding order size */
        else                    
            ++del_iter; /* indicate removal if we cleared bid */   
     
        if(size <= 0) 
            break; /* if we have nothing left to trade*/
    }
    inside->first.erase(inside->first.begin(),del_iter);  

    return size;
}


SOB_TEMPLATE
void 
SOB_CLASS::_threaded_waker(int sleep)
{
    if(sleep <= 0) 
        return;
    else if(sleep < 100)
        std::cerr<< "sleep < 100ms in _threaded_waker; consider larger value\n";
    
    while(this->_master_run_flag){ 
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
        std::lock_guard<std::recursive_mutex> lock(*(this->_mm_mtx));
        /* ---(OUTER) CRITICAL SECTION --- */ 
        for(auto& mm : this->_market_makers){    
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
            std::lock_guard<std::mutex> lock(*(this->_master_mtx));
            /* ---(INNER) CRITICAL SECTION --- */                
            this->_deferred_callback_queue.push_back( /* callback with wake msg */    
                dfrd_cb_elem_type(callback_msg::wake, mm->get_callback(), 
                                  0, this->_itop(this->_last), 0) ); 
            /* ---(INNER) CRITICAL SECTION --- */
        }
        /* ---(OUTER) CRITICAL SECTION --- */ 
    }
}

#define SEARCH_LIMITS_FIRST(id) \
this->_pull_order<limit_chain_type>(id) || this->_pull_order<stop_chain_type>(id)

#define SEARCH_STOPS_FIRST(id) \
this->_pull_order<stop_chain_type>(id) || this->_pull_order<limit_chain_type>(id)

SOB_TEMPLATE
void 
SOB_CLASS::_threaded_order_dispatcher()
{    
    order_queue_elem_type e;
    std::promise<id_type> p;
    order_type ot;
    id_type id;
    bool res;
    
    for( ; ; ){

        {
            std::unique_lock<std::mutex> lock(*(this->_order_queue_mtx));        
            this->_order_queue_cond.wait(lock, [this]{return !this->_order_queue.empty();}); 
            if(!this->_master_run_flag)
                break;
            e = std::move(this->_order_queue.front());
            this->_order_queue.pop();
        }         
        
        p = std::move(std::get<8>(e));
        ot = std::get<0>(e);        
        id = std::get<6>(e);
        if(!id) 
            id = this->_generate_id();
        
        {
            std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
            /* --- CRITICAL SECTION --- */
            switch(ot){            
            case order_type::limit:   
            {         
                this->_insert_limit_order( std::get<1>(e), std::get<2>(e),
                                           std::get<4>(e), std::get<5>(e), 
                                           id, std::get<7>(e) );             
                break;
            }
            case order_type::market:  
            {          
                this->_insert_market_order( std::get<1>(e), std::get<4>(e),
                                            std::get<5>(e), id, std::get<7>(e) );                
                break;
            }
            case order_type::stop:
            {
                this->_insert_stop_order( std::get<1>(e), std::get<3>(e),
                                          std::get<4>(e), std::get<5>(e),
                                          id, std::get<7>(e) );
                break;
            } 
            case order_type::stop_limit:
            {
                this->_insert_stop_order( std::get<1>(e), std::get<3>(e),
                                          std::get<2>(e), std::get<4>(e),
                                          std::get<5>(e), id, std::get<7>(e) );                 
                break;
            } 
            case order_type::null:
            {   /* not the cleanest but the most effective/thread-safe */
                res = std::get<1>(e) ? SEARCH_LIMITS_FIRST(id) : SEARCH_STOPS_FIRST(id);
                id = (id_type)res;
                break;
            } 
            default: 
                throw std::runtime_error("invalid order type in order_queue");
            }
            
            if(ot != order_type::null)
                this->_look_for_triggered_stops();
            
            /* --- CRITICAL SECTION --- */
        }     
        p.set_value(id);    
    }    
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
         std::lock_guard<std::mutex> lock(*(this->_order_queue_mtx));
         this->_order_queue.push(
             order_queue_elem_type(oty, buy, limit, stop, size, cb, id, 
                                   admin_cb, std::move(p)) );
    }    
    this->_order_queue_cond.notify_one();
    
    /* BLOCKING */ 
    ret_id = f.get(); 
    /* BLOCKING */    
        
    this->_clear_callback_queue();    
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
         std::lock_guard<std::mutex> lock(*(this->_order_queue_mtx));
         this->_order_queue.push(
             order_queue_elem_type(oty, buy, limit, stop, size, cb, id, admin_cb,
                   /* dummy --> */ std::move(std::promise<id_type>())) );
    }    
    this->_order_queue_cond.notify_one();
}


SOB_TEMPLATE
void 
SOB_CLASS::_clear_callback_queue()
{
    order_exec_cb_type cb;
    std::deque<dfrd_cb_elem_type> tmp;
    
    bool busy = false;    
    this->_busy_with_callbacks.compare_exchange_strong(busy,true);
    if(busy) /* if false set to true(atomically); if true return */
        return;    

    {     
        std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
        /* --- CRITICAL SECTION --- */    
        std::move(this->_deferred_callback_queue.begin(),
                  this->_deferred_callback_queue.end(), back_inserter(tmp));         
        this->_deferred_callback_queue.clear(); 
        /* --- CRITICAL SECTION --- */
    }    

    for(auto& e : tmp){     
        cb = std::get<1>(e);
        if(cb) 
            cb(std::get<0>(e), std::get<2>(e), std::get<3>(e), std::get<4>(e));                
    }        
    this->_busy_with_callbacks.store(false);
}


SOB_TEMPLATE
void 
SOB_CLASS::_trade_has_occured( plevel plev,
                               size_type size,
                               id_type idbuy,
                               id_type idsell,
                               order_exec_cb_type& cbbuy,
                               order_exec_cb_type& cbsell,
                               bool took_offer)
{  /*
    * CAREFUL: we can't insert orders from here since we have yet to finish
    * processing the initial order (possible infinite loop);
    */
    price_type p = this->_itop(plev);
    
    this->_deferred_callback_queue.push_back( 
        dfrd_cb_elem_type(callback_msg::fill, cbbuy, idbuy, p, size));

    this->_deferred_callback_queue.push_back(
        dfrd_cb_elem_type(callback_msg::fill, cbsell, idsell, p, size));
    
    if(this->_t_and_s_full)
        this->_t_and_s.pop_back();
    else if( this->_t_and_s.size() >= (this->_t_and_s_max_sz - 1) )
        this->_t_and_s_full = true;

    this->_t_and_s.push_back( t_and_s_type(clock_type::now(),p,size) );
    this->_last = plev;
    this->_total_volume += size;
    this->_last_size = size;
}


/*************************************************************************
 ************************************************************************
 *** CURRENTLY working under the constraint that stop priority goes:  ***
 ***     low price to high for buys                                   ***
 ***     high price to low for sells                                  ***
 ***     buys before sells                                            ***
 ***                                                                  ***
 *** The other possibility is FIFO irrespective of price              ***
 ************************************************************************
 ************************************************************************/
SOB_TEMPLATE
void 
SOB_CLASS::_look_for_triggered_stops()
{  /* 
    * PART OF THE ENCLOSING CRITICAL SECTION    
    *
    * we don't check against max/min, because of the cached high/lows 
    */
    plevel low, high;

    for( low = this->_low_buy_stop ; 
         low <= this->_last; 
         ++low )  
    {      
        this->_handle_triggered_stop_chain<true>(low); 
    }

    for( high = this->_high_sell_stop; 
         high >= this->_last; 
         --high )
    {
        this->_handle_triggered_stop_chain<false>(high);    
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
        this->_low_buy_stop = plev + 1;
        if( this->_low_buy_stop > this->_high_buy_stop ){
            /* no more buy stops */
            this->_low_buy_stop = this->_end;
            this->_high_buy_stop = this->_beg-1;
        }
    }else{
        this->_high_sell_stop = plev - 1;
        if( this->_high_sell_stop < this->_low_sell_stop ){
            /* no more sell stops */
            this->_high_sell_stop = this->_beg-1;
            this->_low_sell_stop = this->_end;
        }
    }

    for(stop_chain_type::value_type& e : cchain)
    {
        limit = (plevel)std::get<1>(e.second);
        cb = std::get<3>(e.second);
        sz = std::get<2>(e.second);
       /*
        * note we are keeping the old id
        * 
        * we can't use the blocking version of _push_order or we'll deadlock
        * the order_queue; since we can't guarantee an exec before clearing
        * the queue, we don't (NOT A BIG DEAL IN PRACTICE; NEED TO FIX EVENTUALLY)
        */    
        if(limit){ /* stop to limit */        
            if(cb){ 
                this->_deferred_callback_queue.push_back( 
                    dfrd_cb_elem_type(callback_msg::stop_to_limit, cb, e.first, 
                                      this->_itop(limit), sz) );            
            }
            this->_push_order_no_wait(order_type::limit, std::get<0>(e.second),
                                      limit, nullptr, sz, cb, nullptr, e.first);                     
        }else{ /* stop to market */
            this->_push_order_no_wait(order_type::market, std::get<0>(e.second),
                                      nullptr, nullptr, sz, cb, nullptr, e.first);
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
    if(buy && limit >= this->_ask)
        rmndr = this->_lift_offers(limit,id,size,exec_cb);
    else if(!buy && limit <= this->_bid)
        rmndr = this->_hit_bids(limit,id,size,exec_cb);

    /*
     * then add what remains to bid side; copy callback functor, needs to persist
     */
    if(rmndr > 0){
        limit_chain_type* orders = &limit->first;
        limit_bndl_type bndl = limit_bndl_type(rmndr,exec_cb);
        orders->insert(limit_chain_type::value_type(id,std::move(bndl)));
        
        if(buy){
            if(limit >= this->_bid){
                this->_bid = limit;
                this->_bid_size = _chain<limit_chain_type>::size(orders);
            }
            if(limit < this->_low_buy_limit)
                this->_low_buy_limit = limit;
        }else{
            if(limit <= this->_ask){
                this->_ask = limit;
                this->_ask_size = _chain<limit_chain_type>::size(orders);
            }
            if(limit > this->_high_sell_limit)
                this->_high_sell_limit = limit;
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

    rmndr = buy ? this->_lift_offers(nullptr,id,size,exec_cb)
                : this->_hit_bids(nullptr,id,size,exec_cb);
    if(rmndr)
        throw liquidity_exception("market order couldn't fill");
    
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
    this->_insert_stop_order(buy,stop,nullptr,size,std::move(exec_cb),id,admin_cb);
}


SOB_TEMPLATE
void 
SOB_CLASS::_insert_stop_order( bool buy,
                               plevel stop,
                               plevel limit,
                               size_type size,
                               order_exec_cb_type exec_cb,
                               id_type id,
                               order_admin_cb_type admin_cb)
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
        if(stop < this->_low_buy_stop)    
            this->_low_buy_stop = stop;
        if(stop > this->_high_buy_stop) 
            this->_high_buy_stop = stop;
    }else{ 
        if(stop > this->_high_sell_stop) 
            this->_high_sell_stop = stop;
        if(stop < this->_low_sell_stop)    
            this->_low_sell_stop = stop;
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
    
    std::lock_guard<std::mutex> lock(*(this->_master_mtx));
    /* --- CRITICAL SECTION --- */ 
    _high_low<Side>::template set_using_depth<ChainTy>(this,&h,&l,depth);    
    for( ; h >= l; --h){
        if( !h->first.empty() ){
            d = _chain<limit_chain_type>::size(&h->first);
            md.insert(market_depth_type::value_type(this->_itop(h),d));
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
    
    std::lock_guard<std::mutex> lock(*(this->_master_mtx));
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
    
    std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
    /* --- CRITICAL SECTION --- */    
    std::pair<plevel,FirstChainTy*> pc = _chain<FirstChainTy>::find(this,id);
    p = std::get<0>(pc);
    fc = std::get<1>(pc);
    if(!p || !fc){
        std::pair<plevel,SecondChainTy*> pc = _chain<SecondChainTy>::find(this,id);
        p = std::get<0>(pc);
        sc = std::get<1>(pc);
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

    std::pair<plevel,ChainTy*> cp = _chain<ChainTy>::find(this,id);
    p = std::get<0>(cp);
    c = std::get<1>(cp);

    if(!c || !p)
        return false;

    typename ChainTy::mapped_type bndl = c->at(id);

    /* get the callback and, if stop order, its direction... before erasing */
    cb = this->_get_cb_from_bndl(bndl); 
    if(!IsLimit) 
        is_buystop = std::get<0>(bndl); 
    c->erase(id);

    /* adjust cache vals as necessary */
    if( IsLimit && c->empty() )
        this->_adjust_limit_cache_vals(p);
    else if( !IsLimit && is_buystop )         
        this->_adjust_stop_cache_vals<true>(p,(stop_chain_type*)c);
    else if( !IsLimit && !is_buystop )
        this->_adjust_stop_cache_vals<false>(p,(stop_chain_type*)c); 
        
    /* callback with cancel msg */     
    this->_deferred_callback_queue.push_back( 
        dfrd_cb_elem_type(callback_msg::cancel, cb, id, 0, 0) ); 

    return true;
}


SOB_TEMPLATE
void 
SOB_CLASS::_adjust_limit_cache_vals(plevel plev)
{    
    if( plev > this->_high_sell_limit )
        throw cache_value_error("can't remove limit higher than cached val");
    else if( plev == this->_high_sell_limit )
        --(this->_high_sell_limit); /*dont look for next valid plevel*/
    
    if( plev < this->_low_buy_limit )
        throw cache_value_error("can't remove limit lower than cached val");
    else if( plev == this->_low_buy_limit )
        ++(this->_low_buy_limit); /*dont look for next valid plevel*/     
}


SOB_TEMPLATE
template<bool BuyStop>
void 
SOB_CLASS::_adjust_stop_cache_vals(plevel plev,stop_chain_type* c)
{    
    stop_chain_type::const_iterator biter, eiter, riter;    
    
    biter = c->cbegin();
    eiter = c->cend();

    auto ifcond = [](const stop_chain_type::value_type& v){
                    return std::get<0>(v.second) == BuyStop;
                };
    riter = find_if(biter, eiter, ifcond);
    if(riter != eiter)
        return;
    
    if(BuyStop){

        if(plev > this->_high_buy_stop)
            throw cache_value_error("can't remove stop higher than cached val");
        else if(plev == this->_high_buy_stop)
            --(this->_high_buy_stop); /*dont look for next valid plevel*/ 
        
        if(plev < this->_low_buy_stop)
            throw cache_value_error("can't remove stop lower than cached val");
        else if(plev == this->_low_buy_stop)
            ++(this->_low_buy_stop); /*dont look for next valid plevel*/ 
    
    }else{

        if(plev > this->_high_sell_stop)
            throw cache_value_error("can't remove stop higher than cached val");
        else if(plev == this->_high_sell_stop)
            --(this->_high_sell_stop); /*dont look for next valid plevel */     
        
        if(plev < this->_low_sell_stop)
            throw cache_value_error("can't remove stop lower than cached val");
        else if(plev == this->_low_sell_stop)
            ++(this->_low_sell_stop); /*dont look for next valid plevel*/             
    }                
}


SOB_TEMPLATE
template<bool BuyNotSell>
void 
SOB_CLASS::_dump_limits() const
{ 
    plevel h,l;

    std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    h = BuyNotSell ? this->_bid : this->_high_sell_limit;
    l = BuyNotSell ? this->_low_buy_limit : this->_ask;

    _high_low<>::range_check(this,&h,&l);
    for( ; h >= l; --h){    
        if( !h->first.empty() ){
            std::cout<< this->_itop(h);
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

    std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
    /* --- CRITICAL SECTION --- */
    
    /* from high to low */
    h = BuyNotSell ? this->_high_buy_stop : this->_high_sell_stop;
    l = BuyNotSell ? this->_low_buy_stop : this->_low_sell_stop;

    _high_low<>::range_check(this,&h,&l);    
    for( ; h >= l; --h){ 
        if( !h->second.empty() ){
            std::cout<< this->_itop(h);
            for(const stop_chain_type::value_type& e : h->second){
                plim = (plevel)std::get<1>(e.second);
                std::cout<< " <" << (std::get<0>(e.second) ? "B " : "S ")
                                 << std::to_string(std::get<2>(e.second)) << " @ "
                                 << (plim ? std::to_string(this->_itop(plim)) : "MKT")
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

    incr_offset = round((price - this->_base) * tick_ratio::den/tick_ratio::num);
    plev = this->_beg + incr_offset;

    if(plev < this->_beg)
        throw std::range_error( "chain_pair_type* < _beg" );

    if(plev >= this->_end)
        throw std::range_error( "plevel >= _end" );

    return plev;
}


SOB_TEMPLATE 
typename SOB_CLASS::my_price_type 
SOB_CLASS::_itop(plevel plev) const
{
    price_type incr_offset;
    long long offset;

    if(plev < this->_beg - 1)
        throw std::range_error( "plevel < _beg - 1" );

    if(plev > this->_end )
        throw std::range_error( "plevel > _end" );

    offset = plev - this->_beg;
    incr_offset = (double)offset * tick_ratio::num / tick_ratio::den;

    return this->_base + my_price_type(incr_offset);
}


SOB_TEMPLATE
size_type 
SOB_CLASS::_incrs_in_range(my_price_type lprice, my_price_type hprice)
{
    my_price_type h(this->_round_to_incr(hprice));
    my_price_type l(this->_round_to_incr(lprice));
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
    
    if(this->_lower_incr < 1 || this->_upper_incr < 1)    
        throw invalid_parameters("parameters don't generate enough increments"); 
    
    i = this->_lower_incr + this->_upper_incr + 1;
    
    if(i > max_ticks)
        throw allocation_error("tick range requested would exceed MaxMemory");
    
    return i;
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_makers(market_makers_type&& mms)
{
    std::lock_guard<std::recursive_mutex> lock(*(this->_mm_mtx));
    /* --- CRITICAL SECTION --- */                
    for(pMarketMaker& mm : mms){     
        mm->start(this, this->_itop(this->_last), tick_size);        
        this->_market_makers.push_back(std::move(mm));        
    }
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_maker(MarketMaker&& mm)
{
    std::lock_guard<std::recursive_mutex> lock(*(this->_mm_mtx));
    /* --- CRITICAL SECTION --- */ 
    pMarketMaker pmm = mm._move_to_new();
    this->add_market_maker(std::move(pmm));
    /* --- CRITICAL SECTION --- */ 
}


SOB_TEMPLATE
void 
SOB_CLASS::add_market_maker(pMarketMaker&& mm)
{
    std::lock_guard<std::recursive_mutex> lock(*(this->_mm_mtx));
    /* --- CRITICAL SECTION --- */ 
    mm->start(this, this->_itop(this->_last), tick_size);
    this->_market_makers.push_back(std::move(mm)); 
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
        plev = this->_ptoi(limit);    
    }catch(std::range_error){
        throw invalid_order("invalid limit price");
    }        
 
    return this->_push_order_and_wait(order_type::limit, buy, plev, nullptr,
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
    
    if(this->_market_makers.empty())
        throw invalid_state("orderbook has no market makers");    
    
    return this->_push_order_and_wait(order_type::market, buy, nullptr, nullptr,
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
    return this->insert_stop_order(buy,stop,0,size,exec_cb,admin_cb);
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
    
    if(this->_market_makers.empty())
        throw invalid_state("orderbook has no market makers");

    try{
        plimit = limit ? this->_ptoi(limit) : nullptr;
        pstop = this->_ptoi(stop);         
    }catch(std::range_error){
        throw invalid_order("invalid price");
    }    
    oty = limit ? order_type::stop_limit : order_type::stop;

    return this->_push_order_and_wait(oty,buy,plimit,pstop,size,exec_cb,admin_cb);    
}


SOB_TEMPLATE
bool 
SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{
    return this->_push_order_and_wait(order_type::null, search_limits_first, 
                                      nullptr, nullptr, 0, nullptr, nullptr,id); 
}


SOB_TEMPLATE
order_info_type 
SOB_CLASS::get_order_info(id_type id, bool search_limits_first) 
{     
    {
        std::lock_guard<std::mutex> lock(*(this->_master_mtx)); 
        /* --- CRITICAL SECTION --- */
        return search_limits_first
            ? this->_get_order_info<limit_chain_type, stop_chain_type>(id)        
            : this->_get_order_info<stop_chain_type, limit_chain_type>(id);
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
    
    if(this->pull_order(id))
        id_new = this->insert_limit_order(buy,limit,size,exec_cb,admin_cb);
    
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
    
    if(this->pull_order(id))
        id_new = this->insert_market_order(buy,size,exec_cb,admin_cb);
    
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
    
    if(this->pull_order(id))
        id_new = this->insert_stop_order(buy,stop,size,exec_cb,admin_cb);
    
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
    
    if(this->pull_order(id))
        id_new = this->insert_stop_order(buy,stop,limit,size,exec_cb,admin_cb);
    
    return id_new;
}


SOB_TEMPLATE
void 
SOB_CLASS::dump_cached_plevels() const
{
    std::lock_guard<std::mutex> lock(*(this->_master_mtx));
    /* --- CRITICAL SECTION --- */
    std::cout<< "CACHED PLEVELS" << std::endl;       

    std::cout<< "_high_sell_limit: "
             << std::to_string(this->_itop(this->_high_sell_limit)) 
             << std::endl;

    std::cout<< "_high_buy_stop: "
             << std::to_string(this->_itop(this->_low_buy_stop)) 
             << std::endl;

    std::cout<< "_low_buy_stop: "
             << std::to_string(this->_itop(this->_low_buy_stop)) 
             << std::endl;

    std::cout<< "LAST: "
             << std::to_string(this->_itop(this->_last)) 
             << std::endl;

    std::cout<< "_high_sell_stop: "
             << std::to_string(this->_itop(this->_high_sell_stop)) 
             << std::endl;

    std::cout<< "_low_sell_stop: "
             << std::to_string(this->_itop(this->_low_sell_stop)) 
             << std::endl;

    std::cout<< "_low_buy_limit: "
             << std::to_string(this->_itop(this->_low_buy_limit)) 
             << std::endl;
    /* --- CRITICAL SECTION --- */
}

};
};
