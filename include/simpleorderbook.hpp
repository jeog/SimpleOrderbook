/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_SOB_SIMPLEORDERBOOK
#define JO_SOB_SIMPLEORDERBOOK

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
#include <tuple>
#include <unordered_map>
#include <cstddef>

#include "interfaces.hpp"
#include "resource_manager.hpp"
#include "trimmed_rational.hpp"

#ifdef DEBUG
#define SOB_RESOURCE_MANAGER ResourceManager_Debug
#else
#define NDEBUG
#define SOB_RESOURCE_MANAGER ResourceManager
#endif

#include <assert.h>

namespace sob {
/*
 *   SimpleOrderbook::SimpleOrderbookImpl<std::ratio,size_t> is a class template
 *   that serves as the core implementation.
 *
 *   The ratio-type first parameter defines the tick size.
 *
 *   SimpleOrderbook::BuildFactoryProxy<std::ratio, size_t, CTy>()
 *   returns a struct containing methods for managing SimpleOrderbookImpl
 *   instances. The ratio type and size_t template parameters instantiate the
 *   type of SimpleOrderbookImpl object; CTy is the type of static function
 *   used to create SimpleOrderbookImpl objects.
 *
 *   FactoryProxy.create :  allocate and return an orderbook as FullInterface*
 *   FactoryProxy.destroy : deallocate said object
 *
 *
 *   INTERFACES (interfaces.hpp):
 *
 *   sob::UtilityInterface : provide tick, price, and memory info for that type
 *                          of orderbook
 *
 *            - base of -
 *
 *   sob::QueryInterface : query the state of the orderbook
 *
 *            - base of -
 *
 *   sob::LimitInterface : insert/remove limit orders, pull orders
 *
 *            - base of -
 *
 *   sob::FullInterface : insert/remove market and stop orders, dump orders
 *
 *            - base of -
 *
 *   sob::ManagementInterface : advanced control and diagnostic features
 *                              (grow orderbook, dump internal pointers)
 *
 *
 *   INSERT ORDERS (LimitInterface & FullInterface):
 *
 *   The insert calls are relatively self explanatory except for the two callbacks:
 *
 *   order_exec_cb_type: a functor defined in common.h that will be called
 *                       when a fill or cancelation occurs for the order, or
 *                       when a stop-to-limit is triggered.
 *
 *   order_admin_cb_type: a functor defined in common.h that is guaranteed
 *                        to be called after the order is inserted internally
 *                        but before:
 *                        1) the insert/replace call returns
 *                        2) any stop orders are triggered
 *                        3) any order_exec_cb_type callbacks are made
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
 *   (See simpleorderbook.tpp/simpleorderbook.cpp for implementation details)
 */

class SimpleOrderbook {
    class ImplDeleter;
    static SOB_RESOURCE_MANAGER<FullInterface, ImplDeleter> master_rmanager;

public:
    template<typename... TArgs>
    struct create_func_varargs{
        typedef FullInterface*(*type)(TArgs...);
    };

    template<typename TArg>
    struct create_func_2args
            : public create_func_varargs<TArg, TArg>{
    };

    typedef create_func_2args<double>::type def_create_func_type;

    /* NOTE - we explicity prohibit default construction */
    template< typename CTy=def_create_func_type>
    struct FactoryProxy{
        typedef CTy create_func_type;
        typedef void(*destroy_func_type)(FullInterface *);
        typedef bool(*is_managed_func_type)(FullInterface *);
        typedef std::vector<FullInterface *>(*get_all_func_type)();
        typedef void(*destroy_all_func_type)();
        typedef double(*tick_size_func_type)();
        typedef double(*price_to_tick_func_type)(double);
        typedef long long(*ticks_in_range_func_type)(double, double);
        typedef unsigned long long(*tick_memory_required_func_type)(double, double);
        const create_func_type create;
        const destroy_func_type destroy;
        const is_managed_func_type is_managed;
        const get_all_func_type get_all;
        const destroy_all_func_type destroy_all;
        const tick_size_func_type tick_size;
        const price_to_tick_func_type price_to_tick;
        const ticks_in_range_func_type ticks_in_range;
        const tick_memory_required_func_type tick_memory_required;
        explicit constexpr FactoryProxy( create_func_type create,
                                         destroy_func_type destroy,
                                         is_managed_func_type is_managed,
                                         get_all_func_type get_all,
                                         destroy_all_func_type destroy_all,
                                         tick_size_func_type tick_size,
                                         price_to_tick_func_type price_to_tick,
                                         ticks_in_range_func_type ticks_in_range,
                                         tick_memory_required_func_type
                                             tick_memory_required )
            :
                create(create),
                destroy(destroy),
                is_managed(is_managed),
                get_all(get_all),
                destroy_all(destroy_all),
                tick_size(tick_size),
                price_to_tick(price_to_tick),
                ticks_in_range(ticks_in_range),
                tick_memory_required(tick_memory_required)
            {
            }
    };

    template<typename TickRatio, typename CreatorTy=def_create_func_type>
    static constexpr FactoryProxy<CreatorTy>
    BuildFactoryProxy()
    {
         typedef SimpleOrderbookImpl<TickRatio> ImplTy;
         static_assert( std::is_base_of<FullInterface, ImplTy>::value,
                        "FullInterface not base of SimpleOrderbookImpl");
         return FactoryProxy<CreatorTy>(
                 ImplTy::create,
                 ImplTy::destroy,
                 ImplTy::is_managed,
                 ImplTy::get_all,
                 ImplTy::destroy_all,
                 ImplTy::tick_size_,
                 ImplTy::price_to_tick_,
                 ImplTy::ticks_in_range_,
                 ImplTy::tick_memory_required_
                 );
    }

    static inline void
    Destroy(FullInterface *interface)
    {
        if( interface ){
            master_rmanager.remove(interface);
        }
    }

    static inline void
    DestroyAll()
    { master_rmanager.remove_all(); }

    static inline std::vector<FullInterface *>
    GetAll()
    { return master_rmanager.get_all(); }

    static inline bool
    IsManaged(FullInterface *interface)
    { return master_rmanager.is_managed(interface); }

private:
    template<typename TickRatio>
    class SimpleOrderbookImpl
            : public ManagementInterface{
    protected:
        SimpleOrderbookImpl( TrimmedRational<TickRatio> min, size_t incr );
        ~SimpleOrderbookImpl();

    private:
        SimpleOrderbookImpl(const SimpleOrderbookImpl& sob);
        SimpleOrderbookImpl(SimpleOrderbookImpl&& sob);
        SimpleOrderbookImpl& operator=(const SimpleOrderbookImpl& sob);
        SimpleOrderbookImpl& operator=(SimpleOrderbookImpl&& sob);

        /* manage instances created by factory proxy */
        static SOB_RESOURCE_MANAGER<FullInterface, ImplDeleter> rmanager;

        /* how callback info is stored in the deferred callback queue */
        typedef std::tuple<callback_msg, order_exec_cb_type,
                           id_type, double,size_t>  dfrd_cb_elem_type;

        /* limit bundle type holds the size and callback of each limit order
         * limit 'chain' type holds all limit orders at a price */
        typedef std::pair<size_t, order_exec_cb_type> limit_bndl_type;
        typedef std::map<id_type, limit_bndl_type> limit_chain_type;

        /* stop bundle type holds the size and callback of each stop order
         * stop 'chain' type holds all stop orders at a price(limit or market) */
        typedef std::tuple<bool,void*,size_t,order_exec_cb_type> stop_bndl_type;
        typedef std::map<id_type, stop_bndl_type> stop_chain_type;

        /* chain pair is the limit and stop chain at a particular price
         * use a (less safe) pointer for plevel because iterator
         * is implemented as a class and creates a number of problems internally */
        typedef std::pair<limit_chain_type,stop_chain_type> chain_pair_type;
        typedef std::pair<limit_chain_type,stop_chain_type> *plevel;

        /* a vector of all chain pairs (how we reprsent the 'book' internally) */
        typedef std::vector<chain_pair_type> order_book_type;

        /* type, buy/sell, limit, stop, size, exec cb, id, admin cb, promise */
        typedef std::tuple<order_type,
                           bool,
                           plevel,
                           plevel,
                           size_t,
                           order_exec_cb_type,
                           id_type,
                           order_admin_cb_type,
                           std::promise<id_type>>  order_queue_elem_type;

        /* state fields */
        size_t _bid_size;
        size_t _ask_size;
        size_t _last_size;

        TrimmedRational<TickRatio> _base;

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

        unsigned long long _total_volume;
        id_type _last_id;

        /* time & sales */
        std::vector<timesale_entry_type> _timesales;

        /* store deferred callbacks info until we are clear to execute */
        std::deque<dfrd_cb_elem_type> _deferred_callback_queue;

        /* to prevent recursion within _clear_callback_queue */
        std::atomic_bool _busy_with_callbacks;

        /* async order queue and sync objects */
        std::queue<order_queue_elem_type> _order_queue;
        mutable std::mutex _order_queue_mtx;
        std::condition_variable _order_queue_cond;
        long long _noutstanding_orders;
        bool _need_check_for_stops;

        /* master sync for accessing internals */
        mutable std::mutex _master_mtx;

        /* run secondary threads */
        volatile bool _master_run_flag;

        /* async order queu thread */
        std::thread _order_dispatcher_thread;

        void
        _block_on_outstanding_orders();

        /* handles the async/consumer side of the order queue */
        void
        _threaded_order_dispatcher();

        void
        _route_order(order_queue_elem_type& e, id_type& id);

        /*
         * structs below are used to get around member specialization restrictions
         *
         * TODO: replace some of the default beg/ends with cached extra
         */

        /* chain utilities */
        template<typename ChainTy, typename Dummy = void>
        struct _chain;

        /* adjust high/low plevels */
        template<side_of_market Side=side_of_market::both,
                 typename My=SimpleOrderbookImpl>
        struct _high_low;

        /* build value_types for depth map's */
        template<side_of_market Side, typename My=SimpleOrderbookImpl>
        struct _depth;

        /* generate order_info_type tuples */
        template<typename ChainTy, typename My=SimpleOrderbookImpl>
        struct _order_info;

        /* generic execution helpers */
        template<bool BidSide, bool Redirect = BidSide>
        struct _core_exec;

        /* limit order execution helpers */
        template<bool BuyLimit, typename Dummy = void>
        struct _limit_exec;

        /* stop order execution helpers */
        template<bool BuyStop, bool Redirect = BuyStop>
        struct _stop_exec;

        template<typename ChainTy>
        static inline
        void _assert_valid_chain(){
            static_assert(
                std::is_same<ChainTy, limit_chain_type>::value ||
                std::is_same<ChainTy, stop_chain_type>::value,
                "invalid chain type");
        };

        /* push order onto the order queue and block until execution */
        id_type
        _push_order_and_wait(order_type oty,
                             bool buy,
                             plevel limit,
                             plevel stop,
                             size_t size,
                             order_exec_cb_type cb,
                             order_admin_cb_type admin_cb= nullptr,
                             id_type id = 0);

        /* push order onto the order queue, DONT block */
        void
        _push_order_no_wait(order_type oty,
                            bool buy,
                            plevel limit,
                            plevel stop,
                            size_t size,
                            order_exec_cb_type cb,
                            order_admin_cb_type admin_cb = nullptr,
                            id_type id = 0);

        /* generate order ids; don't worry about overflow */
        inline id_type
        _generate_id()
        { return ++_last_id; }

        /* price-to-index and index-to-price utilities  */
        plevel
        _ptoi(TrimmedRational<TickRatio> price) const;

        TrimmedRational<TickRatio>
        _itop(plevel plev) const;

        /* calculate chain_size of orders at each price level
         * use depth increments on each side of last  */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        std::map<double, typename _depth<Side>::mapped_type >
        _market_depth(size_t depth) const;

        /* total size of bid or ask limits */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        size_t
        _total_depth() const;

        /* return an order_info_type tuple for that order id */
        template<typename FirstChainTy, typename SecondChainTy>
        order_info_type
        _get_order_info(id_type id) const;

        /* remove a particular order */
        template<typename ChainTy,
                 bool IsLimit = std::is_same<ChainTy,limit_chain_type>::value>
        bool
        _pull_order(id_type id);

        /* optimize by checking limit or stop chains first */
        bool
        _pull_order(id_type id, bool limits_first);

        /* called from _pull order to update cached pointers */
        template<bool BuyStop>
        void
        _adjust_stop_cache_vals(plevel plev, stop_chain_type* c);

        void
        _adjust_limit_cache_vals(plevel plev);

        /* called from grow book to reset invalidated pointers */
        void
        _reset_cached_pointers(plevel old_beg,
                               plevel new_beg,
                               plevel old_end,
                               plevel new_end,
                               long long addr_offset);

        /* called by ManagementInterface to increase book size */
        void
        _grow_book(TrimmedRational<TickRatio> min, size_t incr, bool at_beg);

        /* dump a particular chain array to ostream*/
        template<bool BuyNotSell>
        void
        _dump_limits(std::ostream& out) const;

        /* dump a particular chain array ostream*/
        template<bool BuyNotSell>
        void
        _dump_stops(std::ostream& out) const;

        /* handle post-trade tasks */
        void
        _clear_callback_queue();

        void
        _look_for_triggered_stops(bool nothrow);

        template< bool BuyStops>
        void
        _handle_triggered_stop_chain(plevel plev);

        size_t
        _hit_chain(plevel plev,
                   id_type id,
                   size_t size,
                   order_exec_cb_type& exec_cb);

        template<bool BidSize>
        size_t
        _trade(plevel plev,
               id_type id,
               size_t size,
               order_exec_cb_type& exec_cb);


        /* signal trade has occurred(admin only, DONT INSERT NEW TRADES IN HERE!) */
        void
        _trade_has_occured(plevel plev,
                           size_t size,
                           id_type idbuy,
                           id_type idsell,
                           order_exec_cb_type& cbbuy,
                           order_exec_cb_type& cbsell,
                           bool took_offer);

        /* internal insert orders once/if we have an id */
        template<bool BuyLimit>
        void
        _insert_limit_order(plevel limit,
                            size_t size,
                            order_exec_cb_type exec_cb,
                            id_type id);

        template<bool BuyMarket>
        void
        _insert_market_order(size_t size,
                             order_exec_cb_type exec_cb,
                             id_type id);

        template<bool BuyStop>
        void
        _insert_stop_order(plevel stop,
                           size_t size,
                           order_exec_cb_type exec_cb,
                           id_type id);

        template<bool BuyStop>
        void
        _insert_stop_order(plevel stop,
                           plevel limit,
                           size_t size,
                           order_exec_cb_type exec_cb,
                           id_type id);

        /* helper for getting exec callback */
        static inline order_exec_cb_type
        callback_from_bndl(limit_bndl_type& b)
        { return b.second; }

        static inline order_exec_cb_type
        callback_from_bndl(stop_bndl_type& b)
        { return std::get<3>(b); }

        static inline void
        execute_admin_callback(order_admin_cb_type& cb, id_type id)
        { if( cb ) cb(id); }

        template<typename T>
        static inline long long
        bytes_offset(T *l, T *r)
        {
            return (reinterpret_cast<unsigned long long>(l) -
                    reinterpret_cast<unsigned long long>(r));
        }

        template<typename T>
        static inline T*
        bytes_add(T *ptr, long long offset)
        { return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) + offset); }

    public:
        order_info_type
        get_order_info(id_type id, bool search_limits_first=true) const;

        id_type
        insert_limit_order(bool buy,
                           double limit,
                           size_t size,
                           order_exec_cb_type exec_cb = nullptr,
                           order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_market_order(bool buy,
                            size_t size,
                            order_exec_cb_type exec_cb = nullptr,
                            order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          size_t size,
                          order_exec_cb_type exec_cb = nullptr,
                          order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          double limit,
                          size_t size,
                          order_exec_cb_type exec_cb = nullptr,
                          order_admin_cb_type admin_cb = nullptr);

        bool
        pull_order(id_type id,
                   bool search_limits_first=true);

        /* DO WE WANT TO TRANSFER CALLBACK OBJECT TO NEW ORDER ?? */
        id_type
        replace_with_limit_order(id_type id,
                                 bool buy,
                                 double limit,
                                 size_t size,
                                 order_exec_cb_type exec_cb = nullptr,
                                 order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_market_order(id_type id,
                                  bool buy,
                                  size_t size,
                                  order_exec_cb_type exec_cb = nullptr,
                                  order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                size_t size,
                                order_exec_cb_type exec_cb = nullptr,
                                order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                double limit,
                                size_t size,
                                order_exec_cb_type exec_cb = nullptr,
                                order_admin_cb_type admin_cb = nullptr);

        void
        grow_book_above(double new_max);

        void
        grow_book_below(double new_min);

        void
        dump_cached_plevels(std::ostream& out = std::cout) const;

        inline void
        dump_buy_limits(std::ostream& out = std::cout) const
        { _dump_limits<true>(out); }

        inline void
        dump_sell_limits(std::ostream& out = std::cout) const
        { _dump_limits<false>(out); }

        inline void
        dump_buy_stops(std::ostream& out = std::cout) const
        { _dump_stops<true>(out); }

        inline void
        dump_sell_stops(std::ostream& out = std::cout) const
        { _dump_stops<false>(out); }

        inline std::map<double, size_t>
        bid_depth(size_t depth=8) const
        { return _market_depth<side_of_market::bid>(depth); }

        inline std::map<double,size_t>
        ask_depth(size_t depth=8) const
        { return _market_depth<side_of_market::ask>(depth); }

        inline std::map<double,std::pair<size_t, side_of_market>>
        market_depth(size_t depth=8) const
        { return _market_depth<side_of_market::both>(depth); }

        inline double
        bid_price() const
        { return (_bid >= _beg) ? _itop(_bid) : 0.0; }

        inline double
        ask_price() const
        { return (_ask < _end) ? _itop(_ask) : 0.0; }

        inline double
        last_price() const
        { return (_last >= _beg && _last < _end) ? _itop(_last) : 0.0; }

        inline double
        min_price() const
        { return _itop(_beg); }

        inline double
        max_price() const
        { return _itop(_end - 1); }

        inline size_t
        bid_size() const
        { return _bid_size; }

        inline size_t
        ask_size() const
        { return _ask_size; }

        inline size_t
        total_bid_size() const
        { return _total_depth<side_of_market::bid>(); }

        inline size_t
        total_ask_size() const
        { return _total_depth<side_of_market::ask>(); }

        inline size_t
        total_size() const
        { return _total_depth<side_of_market::both>(); }

        inline size_t
        last_size() const
        { return _last_size; }

        inline unsigned long long
        volume() const
        { return _total_volume; }

        inline id_type
        last_id() const
        { return _last_id; }

        inline const std::vector<timesale_entry_type>&
        time_and_sales() const
        { return _timesales; }

        inline double
        tick_size() const
        { return tick_size_(); }

        inline double
        price_to_tick(double price) const
        { return price_to_tick_(price); }

        inline long long
        ticks_in_range(double lower, double upper) const
        { return ticks_in_range_(lower, upper); }

        inline unsigned long long
        tick_memory_required(double lower, double upper) const
        { return tick_memory_required_(lower, upper); }

        inline unsigned long long
        tick_memory_required() const
        { return tick_memory_required_(min_price(), max_price()); }

        bool
        is_valid_price(double price) const
        {
            try{ _ptoi( TrimmedRational<TickRatio>(price) ); }
            catch(std::range_error&){ return false; }
            return true;
        }

        static inline FullInterface*
        create(double min, double max)
        {
            return create( TrimmedRational<TickRatio>(min),
                           TrimmedRational<TickRatio>(max) );
        }

        static FullInterface*
        create( TrimmedRational<TickRatio> min,
                TrimmedRational<TickRatio> max )
        {
            if( min < 0 || min > max ){
                throw std::invalid_argument("!(0 <= min <= max)");
            }
            if( min == 0 ){
               ++min; /* note: we adjust w/o client knowing */
            }

            // make inclusive
            size_t incr = (max - min).as_increments() + 1;
            if( incr < 3 ){
                throw std::invalid_argument("need at least 3 increments");
            }

            FullInterface *tmp = new SimpleOrderbookImpl(min, incr);
            if( tmp ){
                if( !rmanager.add(tmp, master_rmanager) ){
                    delete tmp;
                    throw std::runtime_error("failed to add orderbook");
                }
            }
            return tmp;
        }

        static inline void
        destroy(FullInterface *interface)
        {
            if( interface ){
                rmanager.remove(interface);
            }
        }

        static inline void
        destroy_all()
        { rmanager.remove_all(); }

        static inline std::vector<FullInterface *>
        get_all()
        { return rmanager.get_all(); }

        static inline bool
        is_managed(FullInterface *interface)
        { return rmanager.is_managed(interface); }

        static constexpr double
        tick_size_()
        { return TrimmedRational<TickRatio>::increment_size; }

        static double
        price_to_tick_(double price)
        { return TrimmedRational<TickRatio>(price); }

        static long long
        ticks_in_range_(double lower, double upper)
        {
            auto tr = TrimmedRational<TickRatio>(upper)
                    - TrimmedRational<TickRatio>(lower);
            return tr.as_increments();
        }

        static unsigned long long
        tick_memory_required_(double lower, double upper)
        {
            return static_cast<unsigned long long>(ticks_in_range_(lower, upper))
                    * sizeof(chain_pair_type);
        }

        static_assert(!std::ratio_less<TickRatio,std::ratio<1,10000>>::value,
                      "Increment Ratio < ratio<1,10000> " );

        static_assert(!std::ratio_greater<TickRatio,std::ratio<1,1>>::value,
                      "Increment Ratio > ratio<1,1> " );

    }; /* SimpleOrderbookImpl */

    class ImplDeleter{
        std::string _tag;
        std::string _msg;
        std::ostream& _out;
    public:
        ImplDeleter(std::string tag="",
                    std::string msg="",
                    std::ostream& out=std::cout);
        void
        operator()(FullInterface * i) const;
    };

}; /* SimpleOrderbook */

typedef SimpleOrderbook::FactoryProxy<> DefaultFactoryProxy;

template<typename TickRatio>
SOB_RESOURCE_MANAGER<FullInterface, SimpleOrderbook::ImplDeleter>
SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::rmanager(
        typeid(TickRatio).name()
        );


}; /* sob */

#include "../src/simpleorderbook.tpp"

#endif
