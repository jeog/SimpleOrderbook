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

#include "interfaces.hpp"
#include "resource_manager.hpp"

#ifdef DEBUG
#define SOB_RESOURCE_MANAGER ResourceManager_Debug
#else
#define SOB_RESOURCE_MANAGER ResourceManager
#endif

namespace sob {
/*
 *   SimpleOrderbook::SimpleOrderbookImpl<std::ratio,size_t> is a class template
 *   that serves as the core implementation.
 *
 *   The ratio-type first parameter defines the tick size; the second parameter 
 *   provides a memory limit. Upon construction, if the memory required to build
 *   the internal 'chains' of the book exceeds this memory limit it throws 
 *   sob::allocation_error. (NOTE: MaxMemory is not the maximum total
 *   memory the book can use, as number and types of orders are run-time dependent)
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
 *   interfaces.hpp contains a hierarchy of interfaces for accessing the book:
 *
 *   sob::QueryInterface : the const calls for state info
 *            - base of -
 *   sob::LimitInterface : insert/remove limit orders, pull orders
 *            - base of -
 *   sob::FullInterface : insert/remove all orders, dump orders
 *
 *   The insert calls are relatively self explanatory except for the two callbacks:
 *
 *   order_exec_cb_type: a functor defined in common.h that will be called
 *                       when a fill or cancelation occurs for the order, or
 *                       when a stop-limit is triggered.
 *
 *   order_admin_cb_type: a functor defined in common.h that is guaranteed
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
 *       get_order_info: return a tuple of order type, side, price(s) and size for
 *                       an outstanding order
 *
 *   The dump calls(via SimpleOrderbook::FullInterface) will dump ALL the
 *   orders - of the type named in the call - in a readable form to stdout.
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
        const create_func_type create;
        const destroy_func_type destroy;
        const is_managed_func_type is_managed;
        const get_all_func_type get_all;
        const destroy_all_func_type destroy_all;
        explicit constexpr FactoryProxy( create_func_type create,
                                         destroy_func_type destroy,
                                         is_managed_func_type is_managed,
                                         get_all_func_type get_all,
                                         destroy_all_func_type destroy_all)
            :
                create(create),
                destroy(destroy),
                is_managed(is_managed),
                get_all(get_all),
                destroy_all(destroy_all)
            {
            }
    };

    template< typename TickRatio=hundredth_tick,
              size_t MaxMemory=SOB_MAX_MEM,
              typename CreatorTy=def_create_func_type >
    static constexpr FactoryProxy<CreatorTy>
    BuildFactoryProxy()
    {
         typedef SimpleOrderbookImpl<TickRatio, MaxMemory> ImplTy;
         static_assert( std::is_base_of<FullInterface, ImplTy>::value,
                        "FullInterface not base of SimpleOrderbookImpl");
         return FactoryProxy<CreatorTy>(
                 ImplTy::create,
                 ImplTy::destroy,
                 ImplTy::is_managed,
                 ImplTy::get_all,
                 ImplTy::destroy_all
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
    template<typename TickRatio, size_t MaxMemory>
    class SimpleOrderbookImpl
            : public FullInterface{
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
        unsigned long long _last_id;

        /* time & sales */
        timesale_vector_type _timesales;

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

        void
        _threaded_waker(int sleep);

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
        inline unsigned long long
        _generate_id()
        { return ++_last_id; }

        /* price-to-index and index-to-price utilities  */
        plevel
        _ptoi(TrimmedRational<TickRatio> price) const;

        TrimmedRational<TickRatio>
        _itop(plevel plev) const;

        size_t
        _incrs_in_range( TrimmedRational<TickRatio> lprice,
                         TrimmedRational<TickRatio> hprice );

        /* calculate chain_size of orders at each price level
         * use depth increments on each side of last  */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        std::map<double, typename std::conditional<Side == side_of_market::both,
                                        std::pair<size_t, side_of_market>,
                                        size_t>::type >
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
        inline bool
        _pull_order(bool limits_first, id_type id)
        {
            return limits_first
                ? (_pull_order<limit_chain_type>(id)
                        || _pull_order<stop_chain_type>(id))
                : (_pull_order<stop_chain_type>(id)
                        || _pull_order<limit_chain_type>(id));
        }

        /* helper for getting exec callback (what about admin_cb specialization?) */
        inline order_exec_cb_type
        _get_cb_from_bndl(limit_bndl_type& b)
        { return b.second; }

        inline order_exec_cb_type
        _get_cb_from_bndl(stop_bndl_type& b)
        { return std::get<3>(b); }

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
                            id_type id,
                            order_admin_cb_type admin_cb = nullptr);

        template<bool BuyMarket>
        void
        _insert_market_order(size_t size,
                             order_exec_cb_type exec_cb,
                             id_type id,
                             order_admin_cb_type admin_cb = nullptr);

        template<bool BuyStop>
        void
        _insert_stop_order(plevel stop,
                           size_t size,
                           order_exec_cb_type exec_cb,
                           id_type id,
                           order_admin_cb_type admin_cb = nullptr);

        template<bool BuyStop>
        void
        _insert_stop_order(plevel stop,
                           plevel limit,
                           size_t size,
                           order_exec_cb_type exec_cb,
                           id_type id,
                           order_admin_cb_type admin_cb = nullptr);

    public:
        order_info_type
        get_order_info(id_type id, bool search_limits_first=true) const;

        id_type
        insert_limit_order(bool buy,
                           double limit,
                           size_t size,
                           order_exec_cb_type exec_cb,
                           order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_market_order(bool buy,
                            size_t size,
                            order_exec_cb_type exec_cb,
                            order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          size_t size,
                          order_exec_cb_type exec_cb,
                          order_admin_cb_type admin_cb = nullptr);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          double limit,
                          size_t size,
                          order_exec_cb_type exec_cb,
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
                                 order_exec_cb_type exec_cb,
                                 order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_market_order(id_type id,
                                  bool buy,
                                  size_t size,
                                  order_exec_cb_type exec_cb,
                                  order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                order_admin_cb_type admin_cb = nullptr);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                double limit,
                                size_t size,
                                order_exec_cb_type exec_cb,
                                order_admin_cb_type admin_cb = nullptr);

        inline void
        dump_buy_limits() const
        { _dump_limits<true>(); }

        inline void
        dump_sell_limits() const
        { _dump_limits<false>(); }

        inline void
        dump_buy_stops() const
        { _dump_stops<true>(); }

        inline void
        dump_sell_stops() const
        { _dump_stops<false>(); }

        void
        dump_cached_plevels() const;

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
        incr_size() const
        { return tick_size; }

        inline double
        bid_price() const
        { return _itop(_bid); }

        inline double
        ask_price() const
        { return _itop(_ask); }

        inline double
        last_price() const
        { return _itop(_last); }

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

        inline unsigned long long
        last_id() const
        { return _last_id; }

        inline const timesale_vector_type&
        time_and_sales() const
        { return _timesales; }

        // check these (should ticks_per be int?)
        static constexpr double tick_size = (double)TickRatio::num / TickRatio::den;
        static constexpr double ticks_per_unit = TickRatio::den / TickRatio::num;
        static constexpr size_t max_ticks = MaxMemory / sizeof(chain_pair_type);

        static inline TrimmedRational<TickRatio>
        round_to_incr(TrimmedRational<TickRatio> price)
        {
            return TrimmedRational<TickRatio>(
                       round((double)price * TickRatio::den / TickRatio::num)
                       * TickRatio::num / TickRatio::den
                   );
        }

        static inline size_t
        incrs_in_range( TrimmedRational<TickRatio> lprice,
                        TrimmedRational<TickRatio> hprice )
        {
            auto diff = round_to_incr(hprice) - round_to_incr(lprice);
            return round((double)diff * ticks_per_unit);
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
            if( min.to_incr() == 0 ){
               /* note: we adjust w/o client knowing */
               ++min;
            }

            size_t incr = incrs_in_range(min, max) + 1; // make inclusive
            if( incr < 3 ){
                throw std::invalid_argument("need at least 3 increments");
            }
            if( incr  > max_ticks ){
                throw allocation_error("tick range would exceed MaxMemory");
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

template<typename TickRatio, size_t MaxMemory>
SOB_RESOURCE_MANAGER<FullInterface, SimpleOrderbook::ImplDeleter>
SimpleOrderbook::SimpleOrderbookImpl<TickRatio, MaxMemory>::rmanager(
        typeid(TickRatio).name()
        );


}; /* sob */

#include "../src/simpleorderbook.tpp"

#endif
