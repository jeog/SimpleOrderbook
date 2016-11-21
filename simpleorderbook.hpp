/*
Copyright (C) 2015 Jonathon Ogden     < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_0815_SIMPLE_ORDERBOOK
#define JO_0815_SIMPLE_ORDERBOOK

#include <map>
#include <vector>
#include <memory>
#include <iostream>
#include <utility>
#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <queue>
#include <string>
#include <ratio>
#include <array>
#include <thread>
#include <future>
#include <condition_variable>
#include <chrono>
#include <fstream>

#include "marketmaker.hpp"

namespace NativeLayer{

namespace SimpleOrderbook{

/*
 *   SimpleOrderbook::SimpleOrderbook<std::ratio,size_type> is a class template
 *   that serves as the core implementation.
 *
 *   The ratio-type first parameter defines the tick size; the second parameter 
 *   provides a memory limit. Upon construction, if the memory required to build
 *   the internal 'chains' of the book exceeds this memory limit it throws 
 *   NativeLayer::allocation_error. (NOTE: MaxMemory is not the maximum total
 *   memory the book can use, as number and types of orders are run-time dependent)
 *
 *   types.hpp contains a number of important global objects and typedefs,
 *   including instantiations of SimpleOrderbook with the most popular tick
 *   ratio types and a default memory limit of 1GB.
 *
 *   interfaces.hpp contains a hierarchy of interfaces:
 *
 *       SimpleOrderbook::QueryInterface <-- the const calls for state info
 *              - base of -
 *       SimpleOrderbook::LimitInterface <-- insert/remove limit orders, pull orders
 *              - base of -
 *       SimpleOrderbook::FullInterface <-- insert/remove all orders, dump orders
 *
 *
 *   MarketMakers can be added for liquidity (see market_maker.hpp for details). If 
 *   market orders exceed liquidity NativeLayer::liquidity_exception will be thrown.
 *
 *   The insert calls are relatively self explanatory except for the two callbacks:
 *
 *   order_exec_cb_type: a functor defined in types.h that will be called
 *                       when a fill or cancelation occurs for the order, or
 *                       when a stop-limit is triggered.
 *
 *   order_admin_cb_type: a functor defined in types.h that is guaranteed
 *                        to be called after the order is inserted internally
 *                        but before the insert/replace call returns and any
 *                        order_exec_cb_type callbacks are made.
 *
 *
 *   On success the order id will be returned, 0 on failure. The order id for a 
 *   stop-limit becomes the id for the limit once the stop is triggered.
 *
 *   pull_order(...) attempts to cancel the order, calling back with the id
 *   and callback_msg::cancel on success
 *
 *   The replace calls are basically a pull_order(...) followed by the 
 *   respective insert call, if pull_order is successful. On success the order 
 *   id will be returned, 0 on failure.
 *
 *   Some of the state calls(via SimpleOrderbook::QueryInterface):
 *
 *       bid_price / ask_price: current 'inside' bid / ask price
 *
 *       bid_size / ask_size: current 'inside' bid / ask size
 *
 *       last_price: price of the last trade
 *
 *       volume: total volume traded
 *   
 *       last_id: last order id to be assigned
 *
 *       bid_depth: cumulative size of the (limit) orderbook between inside bid 
 *                  and 'depth' price/tick levels away from the inside bid
 *
 *       ask_depth: (same as bid_depth but on ask side)
 *
 *       market_depth: like a call to bid_depth AND ask_depth
 *
 *       total_bid_size: cumulative size of all bid limits
 *
 *       total_ask_size: (same as total_bid_size but on ask side)
 *
 *       total_size: total_bid_size + total_ask_size *
 *
 *       time_and_sales: a custom vector defined in QuertyInterface that returns
 *                       a pre-defined number of the most recent trades
 *
 *       ::timestamp_to_str: covert a time_stamp_type from to a readable string
 *
 *       get_order_info: return a tuple of order type, side, price(s) and size for
 *                       an outstanding order
 *
 *
 *   The dump calls(via SimpleOrderbook::FullInterface) will dump ALL the
 *   orders - of the type named in the call - in a readable form to stdout.
 *
 *   For the time being copy/move/assign is restricted
 *
 *   ::New(...) can be used as a factory w/ basic type-checking
 *
 *
 *   See simpleorderbook.tpp for implementation details)
 */


#define SOB_TEMPLATE template<typename TickRatio,size_type MaxMemory>
#define SOB_CLASS SimpleOrderbook<TickRatio,MaxMemory>

#define ASSERT_VALID_CHAIN(TYPE) \
    static_assert(std::is_same<TYPE,limit_chain_type>::value || \
                  std::is_same<TYPE,stop_chain_type>::value, \
                  #TYPE " not limit_chain_type or stop_chain_type")

#define SAME_(Ty1,Ty2) std::is_same<Ty1,Ty2>::value


template<typename TickRatio, size_type MaxMemory> /* DEFAULTS IN types.hpp */
class SimpleOrderbook
    : public FullInterface{ 
public:
    typedef SimpleOrderbook<TickRatio,MaxMemory> my_type;
    typedef FullInterface my_base_type;
    typedef TrimmedRational<TickRatio> my_price_type;
    typedef TickRatio tick_ratio;

    static constexpr double tick_size = (double)tick_ratio::num / tick_ratio::den;
    static constexpr double ticks_per_unit = tick_ratio::den / tick_ratio::num;

private:
    static_assert(!std::ratio_less<TickRatio,std::ratio<1,10000>>::value,
                  "Increment Ratio < ratio<1,10000> " );

    static_assert(!std::ratio_greater<TickRatio,std::ratio<1,1>>::value,
                  "Increment Ratio > ratio<1,1> " );

    /* how callback info is stored in the deferred callback queue */
    typedef std::tuple<callback_msg, order_exec_cb_type,
                       id_type, price_type,size_type>  dfrd_cb_elem_type;

    /* limit bundle type holds the size and callback of each limit order
     * limit 'chain' type holds all limit orders at a price */
    typedef std::pair<size_type, order_exec_cb_type> limit_bndl_type;
    typedef std::map<id_type, limit_bndl_type> limit_chain_type;

    /* stop bundle type holds the size and callback of each stop order
     * stop 'chain' type holds all stop orders at a price(limit or market) */
    typedef std::tuple<bool,void*,size_type,order_exec_cb_type> stop_bndl_type;
    typedef std::map<id_type, stop_bndl_type> stop_chain_type;

    /* chain pair is the limit and stop chain at a particular price
     * use a (less safe) pointer for plevel because iterator
     * is implemented as a class and creates a number of problems internally */
    typedef std::pair<limit_chain_type,stop_chain_type> chain_pair_type;
    typedef std::pair<limit_chain_type,stop_chain_type> *plevel;

    static constexpr size_type max_ticks = MaxMemory / sizeof(chain_pair_type);

    /* a vector of all chain pairs (how we reprsent the 'book' internally) */
    typedef std::vector<chain_pair_type> order_book_type;

    /* type, buy/sell, limit, stop, size, exec cb, id, admin cb, promise */
    typedef std::tuple<order_type,
                       bool,
                       plevel,
                       plevel,
                       size_type,
                       order_exec_cb_type,
                       id_type,
                       order_admin_cb_type,
                       std::promise<id_type>>  order_queue_elem_type;

    /* state fields */
    size_type _bid_size;
    size_type _ask_size;
    size_type _last_size;
    size_type _lower_incr;
    size_type _upper_incr;
    size_type _total_incr;

    my_price_type _base;

    /* THE ORDER BOOK */
    order_book_type _book;

    /* cached internal pointers(iterators) of the orderbook */
    plevel _beg;
    plevel _end;
    plevel _last;
    plevel _bid;
    plevel _ask;
    plevel _low_buy_limit;
    plevel _high_sell_limit;
    plevel _low_buy_stop;
    plevel _high_buy_stop;
    plevel _low_sell_stop;
    plevel _high_sell_stop;

    large_size_type _total_volume;
    large_size_type _last_id;

    /* autonomous market makers */
    market_makers_type _market_makers;

    /* store deferred callbacks info until we are clear to execute */
    std::deque<dfrd_cb_elem_type> _deferred_callback_queue;

    /* flag used for restricting async attempts to clear queue until done*/
    std::atomic_bool _cbs_in_progress;

    /* time & sales */
    std::vector< t_and_s_type > _t_and_s;
    size_type _t_and_s_max_sz;
    bool _t_and_s_full;

    /* async order queue and sync objects */
    std::queue<order_queue_elem_type> _order_queue;
    std::unique_ptr<std::mutex> _order_queue_mtx;
    std::condition_variable _order_queue_cond;
    std::thread _order_dispatcher_thread;

    /* handles the async/consumer side of the order queue */
    void _threaded_order_dispatcher();

    /* master sync for accessing internals */
    std::unique_ptr<std::mutex> _master_mtx;
    /* sync mm access */
    std::unique_ptr<std::recursive_mutex> _mm_mtx;

    /* periodic async calls to callback with callback_msg::wake */
    std::thread _waker_thread;
    void _threaded_waker(int sleep);

    /* to prevent recursion within _clear_callback_queue */
    std::atomic_bool _busy_with_callbacks;

    /* indicate we should check for stops hit */
    bool _need_check_for_stops;

    /* run secondary threads */
    volatile bool _master_run_flag;

    /* chain utilities via specializations (in .tpp) */
    template<typename ChainTy, typename Dummy = void>
    struct _chain;

    /* set/adjust high low plevels via specializations (in .tpp) */
    template<side_of_market Side = side_of_market::both, typename My = my_type>
    struct _high_low;

    /* generate order_info_type tuple via specializations (in .tpp) */
    template<typename ChainTy, typename My = my_type>
    struct _order_info;

    /* push order onto the order queue and block until execution */
    id_type 
    _push_order_and_wait(order_type oty, 
                         bool buy, 
                         plevel limit,   
                         plevel stop, 
                         size_type size,
                         order_exec_cb_type cb,
                         order_admin_cb_type admin_cb= nullptr,
                         id_type id = 0);

    /* push order onto the order queue, DONT block */
    void 
    _push_order_no_wait(order_type oty, 
                        bool buy, 
                        plevel limit, 
                        plevel stop,
                        size_type size, 
                        order_exec_cb_type cb,
                        order_admin_cb_type admin_cb = nullptr,
                        id_type id = 0);

    /* generate order ids; don't worry about overflow */
    inline large_size_type 
    _generate_id()
    {
        return ++_last_id;
    }

    /* price-to-index and index-to-price utilities  */
    plevel 
    _ptoi(my_price_type price) const;

    my_price_type 
    _itop(plevel plev) const;

    /* utilities for converting floating point prices and increments */
    inline my_price_type 
    _round_to_incr(my_price_type price)
    {
        return round((double)price * tick_ratio::den / tick_ratio::num) \
               * tick_ratio::num / tick_ratio::den;
    }

    size_type 
    _incrs_in_range(my_price_type lprice, my_price_type hprice);

    size_type 
    _generate_and_check_total_incr();

    /* calculate chain_size of orders at each price level
     * use depth increments on each side of last  */
    template<side_of_market Side, typename ChainTy = limit_chain_type>
    market_depth_type 
    _market_depth(size_type depth) const;

    /* total size of bid or ask limits */
    template<side_of_market Side, typename ChainTy = limit_chain_type>
    size_type 
    _total_depth() const;

    /* return an order_info_type tuple for that order id */
    template<typename FirstChainTy, typename SecondChainTy>
    order_info_type 
    _get_order_info(id_type id);

    /* remove a particular order */
    template<typename ChainTy>
    bool 
    _pull_order(id_type id);

    /* optimize by checking limit or stop chains first */  
	inline bool 
	_pull_order(bool limits_first, id_type id)   
    {
        return limits_first
            ? (_pull_order<limit_chain_type>(id) || _pull_order<stop_chain_type>(id))
            : (_pull_order<stop_chain_type>(id) || _pull_order<limit_chain_type>(id));
    }
   
    /* helper for getting exec callback (what about admin_cb specialization?) */
    inline order_exec_cb_type 
    _get_cb_from_bndl(limit_bndl_type& b)
    { 
        return b.second; 
    }

    inline order_exec_cb_type 
    _get_cb_from_bndl(stop_bndl_type& b)
    { 
        return std::get<3>(b);
    }

    /* called from _pull order to update cached pointers */
    template<bool BuyStop>
    void 
    _adjust_stop_cache_vals(plevel plev, stop_chain_type* c);

    void 
    _adjust_limit_cache_vals(plevel plev);

    /* dump (to stdout) a particular chain array */
    template<bool BuyNotSell>
    void 
    _dump_limits() const;

    /* dump (to stdout) a particular chain array */
    template<bool BuyNotSell>
    void 
    _dump_stops() const;

    /* handle post-trade tasks */
    void 
    _clear_callback_queue();

    void 
    _on_trade_completion();

    void 
    _look_for_triggered_stops();

    template< bool BuyStops>
    void 
    _handle_triggered_stop_chain(plevel plev);

    /* execute if orders match */
    size_type 
    _lift_offers(plevel plev, 
                 id_type id, 
                 size_type size,
                 order_exec_cb_type& exec_cb);

    size_type 
    _hit_bids(plevel plev, 
              id_type id, 
              size_type size,
              order_exec_cb_type& exec_cb);

    size_type
    _hit_chain(plevel plev,
               id_type id,
               size_type size,
               order_exec_cb_type& exec_cb);

    bool 
    _find_new_best_bid();


    bool 
    _find_new_best_ask();

    /* signal trade has occurred(admin only, DONT INSERT NEW TRADES IN HERE!) */
    void 
    _trade_has_occured(plevel plev, 
                       size_type size, 
                       id_type idbuy,
                       id_type idsell, 
                       order_exec_cb_type& cbbuy,
                       order_exec_cb_type& cbsell, 
                       bool took_offer);

    /* internal insert orders once/if we have an id */
    void 
    _insert_limit_order(bool buy, 
                        plevel limit, 
                        size_type size,
                        order_exec_cb_type exec_cb, 
                        id_type id,
                        order_admin_cb_type admin_cb = nullptr);

    void 
    _insert_market_order(bool buy, 
                         size_type size,
                         order_exec_cb_type exec_cb, 
                         id_type id,
                         order_admin_cb_type admin_cb = nullptr);

    void 
    _insert_stop_order(bool buy, 
                       plevel stop, 
                       size_type size,
                       order_exec_cb_type exec_cb, 
                       id_type id,
                       order_admin_cb_type admin_cb = nullptr);

    void 
    _insert_stop_order(bool buy, 
                       plevel stop, 
                       plevel limit, 
                       size_type size,
                       order_exec_cb_type exec_cb, 
                       id_type id,
                       order_admin_cb_type admin_cb = nullptr);

    /***************************************************
     *** RESTRICT COPY / MOVE / ASSIGN ... (for now) ***
     **************************************************/
    SimpleOrderbook(const SimpleOrderbook& sob);
    SimpleOrderbook(SimpleOrderbook&& sob);
    SimpleOrderbook& operator==(const SimpleOrderbook& sob);
    SimpleOrderbook& operator==(SimpleOrderbook&& sob);
    /***************************************************
     *** RESTRICT COPY / MOVE / ASSIGN ... (for now) ***
     **************************************************/

public:
    SimpleOrderbook(my_price_type price, 
                    my_price_type min, 
                    my_price_type max,
                    int sleep=500);

    ~SimpleOrderbook();

    void 
    add_market_makers(market_makers_type&& mms);

    void 
    add_market_maker(MarketMaker&& mms);

    void 
    add_market_maker(pMarketMaker&& mms);

    /* should be const ptr, locking mtx though */
    order_info_type 
    get_order_info(id_type id, bool search_limits_first=true);

    id_type 
    insert_limit_order(bool buy, 
                       price_type limit, 
                       size_type size,
                       order_exec_cb_type exec_cb,
                       order_admin_cb_type admin_cb = nullptr);

    id_type 
    insert_market_order(bool buy, 
                        size_type size,
                        order_exec_cb_type exec_cb,
                        order_admin_cb_type admin_cb = nullptr);

    id_type 
    insert_stop_order(bool buy, 
                      price_type stop, 
                      size_type size,
                      order_exec_cb_type exec_cb,
                      order_admin_cb_type admin_cb = nullptr);

    id_type 
    insert_stop_order(bool buy, 
                      price_type stop, 
                      price_type limit,
                      size_type size, 
                      order_exec_cb_type exec_cb,
                      order_admin_cb_type admin_cb = nullptr);

    bool 
    pull_order(id_type id,
               bool search_limits_first=true);

    /* DO WE WANT TO TRANSFER CALLBACK OBJECT TO NEW ORDER ?? */
    id_type 
    replace_with_limit_order(id_type id, 
                             bool buy, 
                             price_type limit,
                             size_type size, 
                             order_exec_cb_type exec_cb,
                             order_admin_cb_type admin_cb = nullptr);

    id_type 
    replace_with_market_order(id_type id, 
                              bool buy, 
                              size_type size,
                              order_exec_cb_type exec_cb,
                              order_admin_cb_type admin_cb = nullptr);

    id_type 
    replace_with_stop_order(id_type id, 
                            bool buy, 
                            price_type stop,
                            size_type size, 
                            order_exec_cb_type exec_cb,
                            order_admin_cb_type admin_cb = nullptr);

    id_type 
    replace_with_stop_order(id_type id, 
                            bool buy, 
                            price_type stop,
                            price_type limit, 
                            size_type size,
                            order_exec_cb_type exec_cb,
                            order_admin_cb_type admin_cb = nullptr);

    inline void 
    dump_buy_limits() const 
    { 
        _dump_limits<true>(); 
    }

    inline void 
    dump_sell_limits() const 
    { 
        _dump_limits<false>(); 
    }

    inline void 
    dump_buy_stops() const 
    { 
        _dump_stops<true>(); 
    }

    inline void 
    dump_sell_stops() const 
    { 
        _dump_stops<false>(); 
    }

    void 
    dump_cached_plevels() const;

    inline market_depth_type 
    bid_depth(size_type depth=8) const
    {
        return _market_depth<side_of_market::bid>(depth);
    }

    inline market_depth_type 
    ask_depth(size_type depth=8) const
    {
        return _market_depth<side_of_market::ask>(depth);
    }

    inline market_depth_type 
    market_depth(size_type depth=8) const
    {
        return _market_depth<side_of_market::both>(depth);
    }

    inline price_type 
    bid_price() const
    {
        return _itop(_bid);
    }

    inline price_type 
    ask_price() const
    {
        return _itop(_ask);
    }

    inline price_type 
    last_price() const
    {
        return _itop(_last);
    }

    inline size_type 
    bid_size() const
    {
        return _bid_size;
    }

    inline size_type 
    ask_size() const
    {
        return _ask_size;
    }

    inline size_type 
    total_bid_size() const
    {
        return _total_depth<side_of_market::bid>();
    }

    inline size_type 
    total_ask_size() const
    {
        return _total_depth<side_of_market::ask>();
    }

    inline size_type 
    total_size() const
    {
        return _total_depth<side_of_market::both>();
    }

    inline size_type 
    last_size() const
    {
        return _last_size;
    }

    inline large_size_type 
    volume() const
    {
        return _total_volume;
    }

    inline large_size_type 
    last_id() const
    {
        return _last_id;
    }

    inline const time_and_sales_type& 
    time_and_sales() const
    {
        return _t_and_s;
    }
};

template< typename IfaceTy, typename ImplTy >
static IfaceTy* 
New(price_type price,price_type min,price_type max)
{
    static_assert(std::is_base_of<QueryInterface,ImplTy>::value
                  && std::is_base_of<IfaceTy,ImplTy>::value,
                  "New() : invalid type(s)");
    return new ImplTy(price,min,max);
}

};

};

#include "simpleorderbook.tpp"

#endif
