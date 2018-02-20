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
#include <set>
#include <list>
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
#include "tick_price.hpp"
#include "advanced_order.hpp"
#include "order_paramaters.hpp"

#ifdef DEBUG
#undef NDEBUG
#else
#define NDEBUG
#endif

#include <assert.h>

#ifdef NDEBUG
#define SOB_RESOURCE_MANAGER ResourceManager
#else
#define SOB_RESOURCE_MANAGER ResourceManager_Debug
#endif

namespace sob {
/*
 *   SimpleOrderbook::SimpleOrderbookImpl<std::ratio,size_t> is a class template
 *   that serves as the core implementation.
 *
 *   The ratio-type first parameter defines the tick size.
 *
 *   SimpleOrderbook::BuildFactoryProxy<std::ratio, CTy>() returns a struct
 *   containing methods for managing SimpleOrderbookImpl instances. The ratio
 *   type parameter instantiates the SimpleOrderbookImpl type; CTy is the type
 *   of static function used to create SimpleOrderbookImpl objects.
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
 *   There are a number of events that trigger callbacks via:
 *
 *   order_exec_cb_type: a functor defined in common.h that will be called
 *                       when a fill or cancelation occurs for the order, or
 *                       when a stop-to-limit is triggered.
 *
 *   On success the order id will be returned, 0 on failure.
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

    template< typename CTy=create_func_2args<double>::type>
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

    template<typename TickRatio, typename CTy=create_func_2args<double>::type>
    static constexpr FactoryProxy<CTy>
    BuildFactoryProxy()
    {
         typedef SimpleOrderbookImpl<TickRatio> ImplTy;
         static_assert( std::is_base_of<FullInterface, ImplTy>::value,
                        "FullInterface not base of SimpleOrderbookImpl");
         return FactoryProxy<CTy>(
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
        SimpleOrderbookImpl( TickPrice<TickRatio> min, size_t incr );
        ~SimpleOrderbookImpl();

    private:
        SimpleOrderbookImpl(const SimpleOrderbookImpl& sob);
        SimpleOrderbookImpl(SimpleOrderbookImpl&& sob);
        SimpleOrderbookImpl& operator=(const SimpleOrderbookImpl& sob);
        SimpleOrderbookImpl& operator=(SimpleOrderbookImpl&& sob);

        /* manage instances created by factory proxy */
        static SOB_RESOURCE_MANAGER<FullInterface, ImplDeleter> rmanager;

        /* info held for each order in the execution queue */
        typedef struct{
            order_type type;
            bool is_buy;
            double limit;
            double stop;
            size_t sz;
            order_exec_cb_type exec_cb;
            order_condition cond;
            condition_trigger cond_trigger;
            std::unique_ptr<OrderParamaters> cparams1;
            std::unique_ptr<OrderParamaters> cparams2;
            id_type id;
            std::promise<id_type> promise;
        } order_queue_elem;

        struct order_location; /* forward decl */

        typedef std::pair<OrderParamatersByPrice,
                          OrderParamatersByPrice> price_bracket_type;

        typedef std::pair<OrderParamatersByNTicks,
                          OrderParamatersByNTicks> nticks_bracket_type;

        typedef std::pair<size_t, order_location> linked_trailer_type;

        /*
         * base representation of orders internally (inside chains)
         *
         * NOTE: this is just an 'abstract' base for stop/limit bndls to
         *       avoid any sneaky upcasts in all the chain/bndl templates
         *
         * NO VIRTUAL DESTRUCTOR
         */
        struct _order_bndl {
            id_type id;
            size_t sz;
            order_exec_cb_type exec_cb;
            order_condition cond;
            condition_trigger trigger;
            union {
                order_location *linked_order;
                OrderParamaters *contingent_order;
                price_bracket_type *price_bracket_orders;
                nticks_bracket_type *nticks_bracket_orders;
                linked_trailer_type *linked_trailer;
                size_t nticks;
            };
            operator bool() const { return sz; }
            constexpr _order_bndl();
            constexpr _order_bndl(id_type id, size_t sz,
                                  order_exec_cb_type exec_cb,
                                  order_condition cond = order_condition::none,
                                  condition_trigger trigger = condition_trigger::none);
            _order_bndl(const _order_bndl& bndl);
            _order_bndl(_order_bndl&& bndl);
            _order_bndl& operator=(const _order_bndl& bndl);
            _order_bndl& operator=(_order_bndl&& bndl);
            ~_order_bndl();

        private:
            void _copy_union(const _order_bndl& bndl);
            void _move_union(_order_bndl& bndl);
        };

        /* represents a limit order internally */
        struct limit_bndl
                : public _order_bndl {
            using _order_bndl::_order_bndl;
            static limit_bndl null;
        };

        /* represents a stop order internally */
        struct stop_bndl
                : public _order_bndl {
            bool is_buy;
            double limit;
            constexpr stop_bndl();
            constexpr stop_bndl(bool is_buy, double limit, id_type id, size_t sz,
                                order_exec_cb_type exec_cb,
                                order_condition cond = order_condition::none,
                                condition_trigger trigger = condition_trigger::none);
            stop_bndl(const stop_bndl& bndl);
            stop_bndl(stop_bndl&& bndl);
            stop_bndl& operator=(const stop_bndl& bndl);
            stop_bndl& operator=(stop_bndl&& bndl);
            static stop_bndl null;
        };

        /* one order to (quickly) find another */
        struct order_location{ // WHY NOT JUST POINT AT THE OBJECT ?
            bool is_limit_chain;
            double price;
            id_type id;
            bool is_primary;
            constexpr order_location(const order_queue_elem& elem,
                                     bool is_primary);
            constexpr order_location(bool is_limit, double price, id_type id,
                                     bool is_primary);
        };

        /* info held for each exec callback in the deferred callback vector*/
        typedef struct{
            callback_msg msg;
            order_exec_cb_type exec_cb;
            id_type id1;
            id_type id2;
            double price;
            size_t sz;
        } dfrd_cb_elem;

        /* holds all limit orders at a price */
        typedef std::list<limit_bndl> limit_chain_type;

        /* holds all stop orders at a price (limit or market) */
        typedef std::list<stop_bndl> stop_chain_type;

        /*
         * a chain pair is the limit and stop chain at a particular price
         * use a (less safe) pointer for plevel because iterator
         * is implemented as a class and creates a number of problems internally
         */
        typedef std::pair<limit_chain_type,stop_chain_type> chain_pair_type;
        typedef chain_pair_type *plevel;

        TickPrice<TickRatio> _base;

        /* THE ORDER BOOK */
        std::vector<chain_pair_type> _book;

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

        std::unordered_map<id_type, std::pair<double,bool>> _id_cache;

        std::set<id_type> _trailing_sell_stops;
        std::set<id_type> _trailing_buy_stops;

        unsigned long long _total_volume;
        id_type _last_id;
        size_t _last_size;

        /* time & sales */
        std::vector<timesale_entry_type> _timesales;

        /* store deferred callbacks info until we are clear to execute */
        std::vector<dfrd_cb_elem> _deferred_callbacks;

        /* to prevent recursion within _clear_callback_queue */
        std::atomic_bool _busy_with_callbacks;

        /* async order queue and sync objects */
        std::queue<order_queue_elem> _order_queue;
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

        /*
         * internal pointer utilities:
         *   ::get : get current (high/low, stop/limit) order pointers for
         *           various chain/order types and sides of market. Optional
         *           'depth' arg adjusts by some scalar index around bid/ask.
         */
        template<side_of_market Side = side_of_market::both,
                 typename Impl=SimpleOrderbookImpl>
        struct _high_low;

        /*
         * order utilities
         *   ::find : get reference to order bndl for a particular
         *            chain/id, plevel/id, or id
         *   ::find_pos : like 'find' but returns an iterator
         *   ::is_... : myriad (boolean) order type info from elem/bndl
         *   ::limit_price : convert order bndl to limit price
         *   ::stop_price : covert order bndl to stop price
         *   ::as_price_params : convert order bndl to OrderParamatersByPrice
         *   ::as_order_info : return appropriate order_info struct from order ID
         *   ::as_order_type: convert bndl to order type
         *   ::dump : dump appropriate order bndl info to ostream
         */
        struct _order;

        /*
         * chain utilities:
         *   ::get : get appropriate chain from plevel
         *   ::size : get size of chain
         *   ::as_order_type: convert chain type to order type (stop or limit)
         *   ::push : push (move) order bndl onto chain
         *   ::pop : pop (and return) order bundle from chain
         */
        template<typename ChainTy, typename Dummy = void>
        struct _chain;

        /*
         * market depth utilites
         *   ::build_value : create element values for depth-of-market maps
         */
        template<side_of_market Side, typename Impl=SimpleOrderbookImpl>
        struct _depth;

        /*
         * generic execution helpers
         *   ::is_executable_chain : chain/plevel is ready
         *   ::get_inside : the inside bid/ask
         *   ::find_new_best_inside : adjust the internal pointers to the new
         *                            best bids/asks after trade activity
         */
        template<bool BidSide,
                 bool Redirect = BidSide,
                 typename Impl=SimpleOrderbookImpl>
        struct _core_exec;

        /*
         * limit order execution helpers
         *   ::adjust_state_after_insert : adjust internal pointers after
         *                                 order insert
         *   ::adjust_state_after_pull : adjust internal pointers after
         *                               order pull
         *   ::fillable : check if an order can immediate fill against this level
         */
        template<bool BuyLimit=true,
                 typename Impl=SimpleOrderbookImpl>
        struct _limit_exec;

        /*
         * stop order execution helpers
         *   ::adjust_state_after_insert : adjust internal pointers after
         *                                 order insert
         *   ::adjust_state_after_pull : adjust internal pointers after
         *                               order pull
         *   ::adjust_state_after_trigger : adjust internal pointers after
         *                                  stop is triggered
         *   ::stop_chain_is_empty : no active stop orders in this chain
         */
        template<bool BuyStop,
                 bool Redirect = BuyStop,
                 typename Impl=SimpleOrderbookImpl>
        struct _stop_exec;

        /* handles the async/consumer side of the order queue */
        void
        _threaded_order_dispatcher();

        /* all order types go through here */
        bool
        _insert_order(const order_queue_elem& e);

        /* basic order types */
        template<side_of_trade side = side_of_trade::both>
        fill_type
        _route_basic_order(const order_queue_elem& e);

        /* advanced order types */
        void
        _route_advanced_order(const order_queue_elem& e);

        /* if we need immediate (partial/full) fill info for basic order type*/
        bool
        _inject_order(const order_queue_elem& e, bool partial_ok);

        template<bool BidSide>
        size_t
        _trade(plevel plev,
               id_type id,
               size_t size,
               const order_exec_cb_type& exec_cb);

        size_t
        _hit_chain(plevel plev,
                   id_type id,
                   size_t size,
                   const order_exec_cb_type& exec_cb);

        /* DONT INSERT NEW TRADES IN HERE! */
        void
        _trade_has_occured(plevel plev,
                           size_t size,
                           id_type idbuy,
                           id_type idsell,
                           const order_exec_cb_type& cbbuy,
                           const order_exec_cb_type& cbsell);

        bool
        _handle_advanced_order_trigger(_order_bndl& bndl, id_type id);

        bool
        _handle_advanced_order_cancel(_order_bndl& bndl, id_type id);

        void
        _handle_OCO(_order_bndl& bndl, id_type id);

        void
        _handle_OTO(_order_bndl& bndl, id_type id);

        void
        _handle_BRACKET(_order_bndl& bndl, id_type id);

        void
        _handle_TRAILING_BRACKET(_order_bndl& bndl, id_type id);

        void
        _handle_TRAILING_STOP(_order_bndl& bndl, id_type id);

        void
        _exec_OTO_order(const OrderParamaters *op,
                        const order_exec_cb_type& cb,
                        id_type id);

        void
        _exec_BRACKET_order(const OrderParamaters *op1,
                            const OrderParamaters *op2,
                            const order_exec_cb_type& cb,
                            condition_trigger trigger,
                            id_type id);

        void
        _exec_TRAILING_BRACKET_order(const OrderParamaters *op1,
                                     const OrderParamaters *op2,
                                     const order_exec_cb_type& cb,
                                     condition_trigger trigger,
                                     id_type id);

        void
        _exec_TRAILING_STOP_order(const OrderParamaters *op,
                                  const order_exec_cb_type& cb,
                                  condition_trigger trigger,
                                  id_type id);

        template<typename T>
        void
        _exec_OCO_order(const T& t,
                        id_type id_old,
                        id_type id_new,
                        id_type id_pull,
                        double price_pull,
                        bool is_limit);

        void
        _insert_OCO_order(const order_queue_elem& e);

        void
        _insert_OTO_order(const order_queue_elem& e);

        void
        _insert_BRACKET_order(const order_queue_elem& e);

        void
        _insert_TRAILING_BRACKET_order(const order_queue_elem& e);

        void
        _insert_TRAILING_BRACKET_ACTIVE_order(const order_queue_elem& e);

        void
        _insert_TRAILING_STOP_order(const order_queue_elem& e);

        void
        _insert_TRAILING_STOP_ACTIVE_order(const order_queue_elem& e);

        void
        _insert_FOK_order(const order_queue_elem& e);


        /* internal insert orders once/if we have an id */
        template<bool BuyLimit>
        fill_type
        _insert_limit_order(const order_queue_elem& e);

        template<bool BuyMarket>
        void
        _insert_market_order(const order_queue_elem& e);

        template<bool BuyStop>
        void
        _insert_stop_order(const order_queue_elem& e);

        /* handle post-trade tasks */
        void
        _clear_callback_queue();

        void
        _look_for_triggered_stops();

        template<bool BuyStops>
        void
        _handle_triggered_stop_chain(plevel plev);

        void
        _adjust_trailing_stops(bool buy_stops);

        void
        _adjust_trailing_stop(id_type id, bool buy_stop);

        inline void
        _trailing_stop_insert(id_type id, bool is_buy)
        { (is_buy ? _trailing_buy_stops : _trailing_sell_stops).insert(id); }

        inline void
        _trailing_stop_erase(id_type id, bool is_buy)
        { (is_buy ? _trailing_buy_stops : _trailing_sell_stops).erase(id); }

        /* push order onto the order queue and block until execution */
        id_type
        _push_order_and_wait(order_type oty,
                             bool buy,
                             double limit, // TickPrices ??
                             double stop, // TickPrices ??
                             size_t size,
                             order_exec_cb_type cb,
                             order_condition cond = order_condition::none,
                             condition_trigger cond_trigger
                                 = condition_trigger::fill_partial,
                             std::unique_ptr<OrderParamaters>&& cparams1=nullptr,
                             std::unique_ptr<OrderParamaters>&& cparams2=nullptr,
                             id_type id = 0);

        /* push order onto the order queue, DONT block */
        void
        _push_order_no_wait(order_type oty,
                            bool buy,
                            double limit,
                            double stop,
                            size_t size,
                            order_exec_cb_type cb,
                            order_condition cond = order_condition::none,
                            condition_trigger cond_trigger
                                = condition_trigger::fill_partial,
                            std::unique_ptr<OrderParamaters>&& cparams1=nullptr,
                            std::unique_ptr<OrderParamaters>&& cparams2=nullptr,
                            id_type id = 0);

        void
        _push_order(order_type oty,
                    bool buy,
                    double limit,
                    double stop,
                    size_t size,
                    order_exec_cb_type cb,
                    order_condition cond,
                    condition_trigger cond_trigger,
                    std::unique_ptr<OrderParamaters>&& cparams1,
                    std::unique_ptr<OrderParamaters>&& cparams2,
                    id_type id,
                    std::promise<id_type>&& p);

        void
        _block_on_outstanding_orders();

        /* remove a particular order by id... */
        template<typename ChainTy>
        bool
        _pull_order(id_type id, bool pull_linked);

        /* ...(optimized) by checking limit or stop chains first... */
        bool
        _pull_order(id_type id, bool pull_linked, bool limits_first);

        /* ...(optimized) if we already have plevel and order/chain type...*/
        template<typename ChainTy>
        bool
        _pull_order(id_type id, plevel p, bool pull_linked);

        /* ...(optimized) if we know the order/chain type(at run-time) */
        inline bool
        _pull_order(id_type id, double price, bool pull_linked, bool is_limit);

        /* pull OCO (linked) order */
        template<typename ChainTy>
        void
        _pull_linked_order(typename ChainTy::value_type& bndl);

        /* access the _id_cache hash table to find the plevel/chain */
        template<typename ChainTy>
        plevel
        _id_to_plevel(id_type id) const;

        template<typename ChainTy>
        typename ChainTy::value_type&
        _find(id_type id) const;

        _order_bndl&
        _find(id_type id, bool is_limit)
        { return is_limit ? dynamic_cast<_order_bndl&>(_find<limit_chain_type>(id))
                          : dynamic_cast<_order_bndl&>(_find<stop_chain_type>(id)); }

        inline void
        _push_callback( callback_msg msg,
                        const order_exec_cb_type& cb,
                        id_type id1,
                        id_type id2,
                        double price,
                        size_t sz )
        { _deferred_callbacks.push_back({msg, cb, id1, id2, price, sz}); }

        inline bool
        _is_buy_order(plevel p, const limit_bndl& o) const
        { return (p < _ask); }

        inline bool
        _is_buy_order(plevel p, const stop_bndl& o) const
        { return _order::is_buy_stop(o); }

        /* generate order ids; don't worry about overflow */
        inline id_type
        _generate_id()
        { return ++_last_id; }

        /* price-to-index and index-to-price utilities  */
        plevel
        _ptoi(TickPrice<TickRatio> price) const;

        inline plevel
        _ptoi(double price) const
        { return _ptoi( TickPrice<TickRatio>(price)); }

        TickPrice<TickRatio>
        _itop(plevel p) const;

        /* calculate chain_size of orders at each price level
         * use depth ticks on each side of last  */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        std::map<double, typename _depth<Side>::mapped_type >
        _market_depth(size_t depth) const;

        /* total size of bid or ask limits */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        size_t
        _total_depth() const;

        template<typename ChainTy>
        void
        _dump_orders(std::ostream& out,
                     plevel l,
                     plevel h,
                     side_of_trade sot) const;

        /* called from grow book to reset invalidated pointers */
        void
        _reset_internal_pointers(plevel old_beg,
                                 plevel new_beg,
                                 plevel old_end,
                                 plevel new_end,
                                 long long addr_offset);

        /* called by ManagementInterface to increase book size */
        void
        _grow_book(TickPrice<TickRatio> min, size_t incr, bool at_beg);

        /* convert to valid tick price (throw invalid_argument if bad input) */
        double
        _tick_price_or_throw(double price, std::string msg) const;

        /* check for valid plevel */
        void
        _assert_plevel(plevel p) const;

        /* check all the internal order pointers */
        void
        _assert_internal_pointers() const;

        /* build an advanced ticket from a linked/contingent order bndl (OCO/OTO) */
        template<typename ChainTy>
        AdvancedOrderTicket
        _bndl_to_aot(const typename _chain<ChainTy>::bndl_type& bndl) const;

        /* check/build internal param object from user input for advanced
         * order types (uses _tick_price_or_throw to check user input) */
        std::unique_ptr<OrderParamaters>
        _build_nticks_params(bool buy,
                             size_t size,
                             const OrderParamaters *order) const;

        std::unique_ptr<OrderParamaters>
        _build_price_params(const OrderParamaters *order) const;

        std::pair<std::unique_ptr<OrderParamaters>,
                   std::unique_ptr<OrderParamaters>>
        _build_advanced_params(bool buy,
                               size_t size,
                               const AdvancedOrderTicket& advanced) const;

        /* check prices levels for limit-OCO orders are valid */
        void
        _check_limit_order(bool buy,
                           double limit,
                           std::unique_ptr<OrderParamaters> & op,
                           order_condition oc) const;

        inline plevel
        _generate_trailing_stop(bool buy_stop, size_t nticks)
        { return nticks ? (_last + (buy_stop ? nticks : -nticks)) : 0; }

        inline plevel
        _generate_trailing_limit(bool buy_limit, size_t nticks)
        { return nticks ? (_last + (buy_limit ? -nticks : nticks)) : 0; }

        template<typename T>
        static constexpr long long
        bytes_offset(T *l, T *r)
        { return (reinterpret_cast<unsigned long long>(l) -
                  reinterpret_cast<unsigned long long>(r)); }

        template<typename T>
        static constexpr T*
        bytes_add(T *ptr, long long offset)
        { return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) + offset); }

        /* only an issue if size of book is > (MAX_LONG * sizeof(*plevel)) bytes */
        static constexpr long
        plevel_offset(plevel l, plevel r)
        { return static_cast<long>(bytes_offset(l, r) / sizeof(*l)); }

    public:
        id_type
        insert_limit_order(bool buy,
                           double limit,
                           size_t size,
                           order_exec_cb_type exec_cb = nullptr,
                           const AdvancedOrderTicket& advanced
                               = AdvancedOrderTicket::null);

        id_type
        insert_market_order(bool buy,
                            size_t size,
                            order_exec_cb_type exec_cb = nullptr,
                            const AdvancedOrderTicket& advanced
                                = AdvancedOrderTicket::null);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          size_t size,
                          order_exec_cb_type exec_cb = nullptr,
                          const AdvancedOrderTicket& advanced
                              = AdvancedOrderTicket::null);

        id_type
        insert_stop_order(bool buy,
                          double stop,
                          double limit,
                          size_t size,
                          order_exec_cb_type exec_cb = nullptr,
                          const AdvancedOrderTicket& advanced
                              = AdvancedOrderTicket::null);

        bool
        pull_order(id_type id,
                   bool search_limits_first=true);

        order_info
        get_order_info(id_type id, bool search_limits_first=true) const;

        id_type
        replace_with_limit_order(id_type id,
                                 bool buy,
                                 double limit,
                                 size_t size,
                                 order_exec_cb_type exec_cb = nullptr,
                                 const AdvancedOrderTicket& advanced
                                     = AdvancedOrderTicket::null);

        id_type
        replace_with_market_order(id_type id,
                                  bool buy,
                                  size_t size,
                                  order_exec_cb_type exec_cb = nullptr,
                                  const AdvancedOrderTicket& advanced
                                      = AdvancedOrderTicket::null);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                size_t size,
                                order_exec_cb_type exec_cb = nullptr,
                                const AdvancedOrderTicket& advanced
                                    = AdvancedOrderTicket::null);

        id_type
        replace_with_stop_order(id_type id,
                                bool buy,
                                double stop,
                                double limit,
                                size_t size,
                                order_exec_cb_type exec_cb = nullptr,
                                const AdvancedOrderTicket& advanced
                                    = AdvancedOrderTicket::null);

        void
        grow_book_above(double new_max);

        void
        grow_book_below(double new_min);

        void
        dump_internal_pointers(std::ostream& out = std::cout) const;

        inline void
        dump_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
                out, std::min(_low_buy_limit, _ask),
                std::max(_high_sell_limit, _bid), side_of_trade::both); }

        inline void
        dump_buy_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
                out, _low_buy_limit, _bid, side_of_trade::buy); }

        inline void
        dump_sell_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
                out, _ask, _high_sell_limit, side_of_trade::sell); }

        inline void
        dump_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
                out, std::min(_low_buy_stop, _low_sell_stop),
                std::max(_high_buy_stop, _high_sell_stop), side_of_trade::both); }

        inline void
        dump_buy_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
                out, _low_buy_stop , _high_buy_stop, side_of_trade::buy); }

        inline void
        dump_sell_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
                out, _low_sell_stop, _high_sell_stop, side_of_trade::sell); }

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
        { return (_bid >= _beg) ? _chain<limit_chain_type>::size(&_bid->first) : 0; }

        inline size_t
        ask_size() const
        { return (_ask < _end) ? _chain<limit_chain_type>::size(&_ask->first) : 0; }

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

        inline long long
        ticks_in_range() const
        { return ticks_in_range_(min_price(), max_price()); }

        inline unsigned long long
        tick_memory_required(double lower, double upper) const
        { return tick_memory_required_(lower, upper); }

        inline unsigned long long
        tick_memory_required() const
        { return tick_memory_required_(min_price(), max_price()); }

        bool
        is_valid_price(double price) const
        {
            long long offset = (TickPrice<TickRatio>(price) - _base).as_ticks();
            plevel p = _beg + offset;
            return (p >= _beg && p < _end);
        }

        static inline FullInterface*
        create(double min, double max)
        {
            return create( TickPrice<TickRatio>(min),
                           TickPrice<TickRatio>(max) );
        }

        static FullInterface*
        create( TickPrice<TickRatio> min,
                TickPrice<TickRatio> max )
        {
            if( min < 0 || min > max ){
                throw std::invalid_argument("!(0 <= min <= max)");
            }
            if( min == 0 ){
               ++min; /* note: we adjust w/o client knowing */
            }

            // make inclusive
            size_t incr = static_cast<size_t>((max - min).as_ticks()) + 1;
            if( incr < 3 ){
                throw std::invalid_argument("need at least 3 ticks");
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
        tick_size_() noexcept
        { return TickPrice<TickRatio>::tick_size; }

        static constexpr double
        price_to_tick_(double price)
        { return TickPrice<TickRatio>(price); }

        static constexpr long long
        ticks_in_range_(double lower, double upper)
        {
            return ( TickPrice<TickRatio>(upper)
                     - TickPrice<TickRatio>(lower) ).as_ticks();
        }

        static constexpr unsigned long long
        tick_memory_required_(double lower, double upper)
        {
            return static_cast<unsigned long long>(ticks_in_range_(lower, upper))
                    * sizeof(chain_pair_type);
        }
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

template<typename TickRatio>
typename SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::limit_bndl
SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::limit_bndl::null;

template<typename TickRatio>
typename SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::stop_bndl
SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::stop_bndl::null;

struct order_info {
    order_type type;
    bool is_buy;
    double limit;
    double stop;
    size_t size;
    AdvancedOrderTicket advanced;

    inline operator bool()
    { return type != order_type::null; }

    order_info(order_type type,
               bool is_buy,
               double limit,
               double stop,
               size_t size,
               const AdvancedOrderTicket& advanced);

    order_info(const order_info& oi);
};

}; /* sob */

#include "../src/simpleorderbook_tpp/util.tpp"
#include "../src/simpleorderbook_tpp/bndl.tpp"
#include "../src/simpleorderbook_tpp/public.tpp"
#include "../src/simpleorderbook_tpp/core.tpp"

#endif
