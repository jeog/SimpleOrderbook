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
#include <type_traits>

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

#define NDEBUG_RMANAGER

#if defined(NDEBUG) || defined(NDEBUG_RMANAGER)
#define SOB_RESOURCE_MANAGER ResourceManager
#else
#define SOB_RESOURCE_MANAGER ResourceManager_Debug
#endif


namespace sob {

/*
 *   SimpleOrderbook::SimpleOrderbookImpl<std::ratio,size_t> :
 *
 *      A class template that serves as the core implementation. The ratio-type
 *      first parameter defines the tick size.
 *
 *
 *   SimpleOrderbook::BuildFactoryProxy<std::ratio, CTy>() :
 *
 *      Builds a factory proxy used to create and manage a SimpleOrderbookImpl<>
 *      object. The std::ratio parameter is the desired tick size of the
 *      orderbook. The CTy is the type of the 'create' static factory method
 *      used to create the orderbook object.
 *
 *
 *   SimpleOrderbook::FactoryProxy<CTy> :
 *
 *      A struct containing methods for managing SimpleOrderbookImpl instances
 *      of a particular std::ratio type:
 *
 *      .create :  allocate and return an orderbook as FullInterface*
 *      .destroy : deallocate said object
 *      .is_managed : is the the passed orderbook pointer currently managed
 *      .get_all : get a vector of pointers of all the currently managed orderbooks
 *      .destroy_all : deallocate all the currently managed orderbooks
 *      .tick_size : get the current tick size of the orderbook as double
 *      .price_to_tick : convert a double to a valid tick price
 *      .ticks_in_range : calculate number of ticks between two doubles
 *      .tick_memory_required : calculate number of bytes required to initialize
 *                              and orderbook between this range of doubles
 *
 *
 *   UtilityInterface :
 *
 *      tick, price, and memory info
 *
 *   QueryInterface :
 *
 *      query state of the orderbook (last, bid_size, time_and_sales etc.)
 *
 *   LimitInterface :
 *
 *      insert limit orders(and advanced versions), pull orders
 *
 *   FullInterface :
 *
 *      insert market and stop orders(and advanced versions); dump orders to
 *      std::ostream; A POINTER TO THIS INTERFACE IS RETURNED BY THE FACTORY
 *      PROXY'S '.create' METHOD.
 *
 *   ManagementInterface :
 *
 *      advanced control and diagnostic features (e.g grow the orderbook)
 *
 *
 *   order_exec_cb_type :
 *
 *      a callback functor defined in common.h that will be called when an
 *      execution, cancellation, or advanced order action occurs. STOP-LIMITS
 *      AND CERTAIN ADVANCED ORDERS NEED TO KEEP TRACK OF THE TWO 'id_type'
 *      ARGS FOR CHANGES IN ORDER ID# WHEN CERTAIN CONDITIONS ARE TRIGGERED.
 */

namespace detail {
    /*
     * struct specializations used internally by orderbook (specials.tpp)
     *
     * declared as friends inside SimpleOrderbookBase (documented below)
     */
    struct sob_types;
    struct order;
    template<sob::side_of_market Side> struct range;
    template<sob::side_of_market Side> struct depth;
    template<typename ChainTy, bool BaseOnly> struct chain;
    namespace exec{
        template<bool BidSide, bool Redirect> struct core;
        template<bool BidSide> struct aon;
        template<bool BidSide> struct limit;
        template<bool BuyStop> struct stop;
    };
};

class SimpleOrderbook {
    class ImplDeleter;
    static SOB_RESOURCE_MANAGER<FullInterface, ImplDeleter> master_rmanager;

public:
    template<typename... TArgs>
    struct create_func_varargs{
        using type = FullInterface*(*)(TArgs...);
    };

    template<typename TArg>
    struct create_func_2args
            : public create_func_varargs<TArg, TArg>{
    };

    template< typename CTy=create_func_2args<double>::type>
    struct FactoryProxy{
        using create_func_type = CTy;
        using destroy_func_type = void(*)(FullInterface*);
        using is_managed_func_type = bool(*)(FullInterface *);
        using get_all_func_type = std::vector<FullInterface *>(*)();
        using destroy_all_func_type = void(*)();
        using tick_size_func_type = double(*)();
        using price_to_tick_func_type = double(*)(double);
        using ticks_in_range_func_type = long long(*)(double, double);

        const create_func_type create;
        const destroy_func_type destroy;
        const is_managed_func_type is_managed;
        const get_all_func_type get_all;
        const destroy_all_func_type destroy_all;
        const tick_size_func_type tick_size;
        const price_to_tick_func_type price_to_tick;
        const ticks_in_range_func_type ticks_in_range;

        explicit constexpr FactoryProxy( create_func_type create,
                                         destroy_func_type destroy,
                                         is_managed_func_type is_managed,
                                         get_all_func_type get_all,
                                         destroy_all_func_type destroy_all,
                                         tick_size_func_type tick_size,
                                         price_to_tick_func_type price_to_tick,
                                         ticks_in_range_func_type ticks_in_range )
            :
                create(create),
                destroy(destroy),
                is_managed(is_managed),
                get_all(get_all),
                destroy_all(destroy_all),
                tick_size(tick_size),
                price_to_tick(price_to_tick),
                ticks_in_range(ticks_in_range)
            {
            }
    };

    template<typename TickRatio, typename CTy=create_func_2args<double>::type>
    static constexpr FactoryProxy<CTy>
    BuildFactoryProxy()
    {
        using ImplTy = SimpleOrderbookImpl<TickRatio>;
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
                ImplTy::ticks_in_range_
                );
    }

    static inline void
    Destroy(FullInterface *interface)
    { if( interface ) master_rmanager.remove(interface); }

    static inline void
    DestroyAll()
    { master_rmanager.remove_all(); }

    static inline std::vector<FullInterface *>
    GetAll()
    { return master_rmanager.get_all(); }

    static inline bool
    IsManaged(FullInterface *interface)
    { return master_rmanager.is_managed(interface); }

    friend struct detail::sob_types;


private:
    /*
     * define as much as we can in the Base so as to limit the amount of
     * header definitions we need to include for derived template class.
     */
    class SimpleOrderbookBase
        : public ManagementInterface{
    protected:

        struct order_queue_elem_base_{
            order_type type;
            bool is_buy;
            double limit;
            double stop;
            size_t sz;
            order_exec_cb_type exec_cb;
            id_type id;

            order_queue_elem_base_(
                order_type ot,
                bool is_buy,
                double limit,
                double stop,
                size_t sz,
                order_exec_cb_type exec_cb,
                id_type id
                );

            order_queue_elem_base_();
        };

        /* order info passed to external/execution queue */
        struct external_order_queue_elem
                : public order_queue_elem_base_{
            AdvancedOrderTicket aot; // copy ??
            std::promise<id_type> promise;

            external_order_queue_elem(
                order_type ot,
                bool is_buy,
                double limit,
                double stop,
                size_t sz,
                order_exec_cb_type exec_cb,
                id_type id,
                const AdvancedOrderTicket& aot,
                std::promise<id_type>&& promise
                );

            external_order_queue_elem();
            external_order_queue_elem( external_order_queue_elem&& ) = default;
            external_order_queue_elem&
            operator=( external_order_queue_elem&& )  = default;
        };

        /* order info used internally, passed to internal/execution queue */
        struct order_queue_elem
                : public order_queue_elem_base_{
            order_condition condition;
            condition_trigger trigger;
            std::unique_ptr<OrderParamaters> cparams1;
            std::unique_ptr<OrderParamaters> cparams2;

            order_queue_elem(
                order_type ot,
                bool is_buy,
                double limit,
                double stop,
                size_t sz,
                order_exec_cb_type exec_cb, // TODO
                id_type id,
                order_condition cond,
                condition_trigger trigger,
                std::unique_ptr<OrderParamaters>&& cparams1,
                std::unique_ptr<OrderParamaters>&& cparams2
                );

            order_queue_elem(const external_order_queue_elem& e,
                             const SimpleOrderbookBase* sob);
        };




        struct order_location; /* forward decl */

        using price_bracket_type = std::pair<OrderParamatersByPrice,
                                             OrderParamatersByPrice>;

        using nticks_bracket_type = std::pair<OrderParamatersByNTicks,
                                              OrderParamatersByNTicks> ;

        using linked_trailer_type = std::pair<size_t, order_location>;

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
            _order_bndl();
            _order_bndl(id_type id, size_t sz,
                        order_exec_cb_type exec_cb,
                        order_condition cond = order_condition::none,
                        condition_trigger trigger = condition_trigger::none);
            _order_bndl(const _order_bndl& bndl);
            _order_bndl(_order_bndl&& bndl);
            _order_bndl& operator=(const _order_bndl& bndl) = delete;
            _order_bndl& operator=(_order_bndl&& bndl) = delete;
            ~_order_bndl();

        private:
            void _copy_union(const _order_bndl& bndl);
            void _move_union(_order_bndl& bndl);
        };

        /* represents a limit order internally */
        struct limit_bndl
                : public _order_bndl {
            using _order_bndl::_order_bndl;
            limit_bndl() = default;
            limit_bndl( const _order_bndl& bndl ) : _order_bndl(bndl){}
            limit_bndl( _order_bndl&& bndl ) : _order_bndl( std::move(bndl) ){}
            static limit_bndl null;
        };

        /* represents a stop order internally */
        struct stop_bndl
                : public _order_bndl {
            bool is_buy;
            double limit;
            stop_bndl();
            stop_bndl(bool is_buy, double limit, id_type id, size_t sz,
                      order_exec_cb_type exec_cb,
                      order_condition cond = order_condition::none,
                      condition_trigger trigger = condition_trigger::none);
            stop_bndl(const stop_bndl& bndl);
            stop_bndl(stop_bndl&& bndl);
            stop_bndl& operator=(const stop_bndl& bndl) = delete;
            stop_bndl& operator=(stop_bndl&& bndl) = delete;
            static stop_bndl null;
        };

        /* represents an AON (limit) order internall */
        struct aon_bndl
                : public _order_bndl {
            using _order_bndl::_order_bndl;
            aon_bndl() = default;
            aon_bndl( const _order_bndl& bndl ) : _order_bndl(bndl){}
            aon_bndl( _order_bndl&& bndl ) : _order_bndl( std::move(bndl) ){}
            static aon_bndl null;
        };


        /* one order to (quickly) find another */
        struct order_location{ // WHY NOT JUST POINT AT THE OBJECT ?
            bool is_limit_chain;
            double price;
            id_type id;
            bool is_primary;
            order_location(const order_queue_elem& elem, bool is_primary);
            order_location(bool is_limit, double price, id_type id, bool is_primary);
        };

        /* info held for each exec callback in the deferred callback vector*/
        struct dfrd_cb_elem{
            callback_msg msg;
            order_exec_cb_type exec_cb;
            id_type id1;
            id_type id2;
            double price;
            size_t sz;
            dfrd_cb_elem(callback_msg msg, const order_exec_cb_type& exec_cb,
                         id_type id1, id_type id2, double price, size_t sz)
                : msg(msg), exec_cb(exec_cb), id1(id1), id2(id2),
                  price(price), sz(sz)
                {}
        };


        /* holds all limit orders at a price */
        using limit_chain_type = std::list<limit_bndl>;

        /* holds all stop orders at a price (limit or market) */
        using stop_chain_type = std::list<stop_bndl>;

        /* holds all buy AND sell aon orders at a price */
        using aon_chain_type = std::list<aon_bndl>;


        class level {
            /*
             * *NEW APPROACH* to managing chains at each price level (APR 2019)
             *
             *   * replace 'chain_pair_type' with a 'level' class
             *
             *   * store chains on the heap and only allow move(s) so
             *   iterators (stored in id_cache) aren't invalidated if a book
             *   resize requires a new allocation/initialization
             *
             *   * TODO store chain info to limit chain traversals
             */
            std::unique_ptr<limit_chain_type> _l_chain;
            std::unique_ptr<stop_chain_type> _s_chain;
            std::unique_ptr<aon_chain_type> _aon_b_chain;
            std::unique_ptr<aon_chain_type> _aon_s_chain;

        public:
            level();
            level( const level& ) = delete;
            level& operator=( const level& ) = delete;
            level( level&& l ) = default;
            level& operator=( level&& l ) = default ;
            
            limit_chain_type* get_limit_chain() const { return _l_chain.get(); }
            stop_chain_type* get_stop_chain() const { return _s_chain.get(); }

            bool limit_chain_is_empty() const { return _l_chain->empty(); }
            bool stop_chain_is_empty() const { return _s_chain->empty(); }

            template<bool BuyChain> aon_chain_type* get_aon_chain() const;
            template<bool BuyChain> bool aon_chain_is_empty() const;
            template<bool BuyChain> void create_aon_chain();
            template<bool BuyChain> void destroy_aon_chain();
            template<bool BuyChain> void push_aon_bndl(aon_bndl&& bndl);
        };
        using plevel = level*;


        SimpleOrderbookBase( size_t incr,
                             std::function<double(plevel)> itop,
                             std::function<plevel(double)> ptoi,
                             std::function<long long(double, double)> ticks_in_range,
                             std::function<bool(double)> is_valid_price
                             );
        ~SimpleOrderbookBase();

         /* THE ORDER BOOK */
        std::vector<level> _book;

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
        plevel _low_buy_aon;
        plevel _high_buy_aon;
        plevel _low_sell_aon;
        plevel _high_sell_aon;

        struct chain_iter_wrap {
        private:
            _order_bndl& _get_base_bndl() const;

        public:
            enum class itype {
                limit, stop, aon_buy, aon_sell
            };

            template<bool IsBuy>
            struct aon_itype{
                static constexpr itype value = IsBuy
                    ? itype::aon_buy : itype::aon_sell;
            };

            union{
                limit_chain_type::iterator l_iter;
                stop_chain_type::iterator s_iter;
                aon_chain_type::iterator a_iter;
            };
            itype type;
            plevel p;

            chain_iter_wrap(limit_chain_type::iterator iter, plevel p);
            chain_iter_wrap(stop_chain_type::iterator iter, plevel p);
            chain_iter_wrap(aon_chain_type::iterator iter, plevel p, bool is_buy);

            bool is_limit() const { return type == itype::limit; }
            bool is_stop() const { return type == itype::stop; }
            bool is_aon_buy() const { return type == itype::aon_buy; }
            bool is_aon_sell() const { return type == itype::aon_sell; }
            bool is_aon() const { return is_aon_buy() || is_aon_sell(); }

            operator bool() const { return _get_base_bndl().operator bool();}

            _order_bndl& operator*() { return _get_base_bndl(); }
            _order_bndl* operator->() { return &_get_base_bndl(); }
        };

        class OrderNotInCache
            : public std::logic_error{
        public:
            OrderNotInCache(id_type id);
        };

        // TODO test cache is in-line after advanced execution
        // UPDATE APR 18 2019 - POINT AT ACTUAL ORDER
        std::unordered_map<id_type, chain_iter_wrap> _id_cache;

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
        std::queue<external_order_queue_elem> _external_order_queue;
        mutable std::mutex _external_order_queue_mtx;
        std::condition_variable _external_order_queue_cond;

        /* sync order queue for internal entry */
        std::queue<order_queue_elem> _internal_order_queue;

        bool _need_check_for_stops;

        /* master sync for accessing internals */
        mutable std::mutex _master_mtx;

        /* run secondary threads */
        volatile bool _master_run_flag;

        /* async order queu thread */
        std::thread _order_dispatcher_thread;

        std::function<double(plevel)> _itop;
        std::function<plevel(double)> _ptoi;
        std::function<long long(double, double)> _ticks_in_range;
        std::function<bool(double)> _is_valid_price;

        friend struct detail::sob_types;

        /*
         * internal pointer utilities:
         *   ::get : get current (high/low, stop/limit) order pointers for
         *           various chain/order types and sides of market. Optional
         *           'depth' arg adjusts by some scalar index around bid/ask.
         */
        template<side_of_market Side> friend struct detail::range;

        /*
         * order utilities
         *   ::is_... : myriad (boolean) order type info from elem/bndl
         *   ::limit_price : convert order bndl to limit price
         *   ::stop_price : covert order bndl to stop price
         *   ::as_price_params : convert order bndl to OrderParamatersByPrice
         *   ::as_order_info : return appropriate order_info struct from order ID
         *   ::as_order_type: convert bndl to order type
         *   ::dump : dump appropriate order bndl info to ostream
         */
        friend struct detail::order;

        /*
         * chain utilities:
         *   ::push : push (move) order bndl onto chain
         *   ::pop : pop (and return) order bundle from chain
         *   ::get : get appropriate chain from plevel
         *   ::size : get size of chain
         *   ::as_order_type : get order_type (e.g order_type::limit) of chain
         */
        template<typename ChainTy, bool BaseOnly> friend struct detail::chain;

        /*
         * market depth utilites
         *   ::build_value : create element values for depth-of-market maps
         */
        template<side_of_market Side> friend struct detail::depth;

        /*
         * generic execution helpers
         *   ::begin : most inside plevel w/ orders
         *   ::end : most outside plevel w/ orders
         *   ::inside_of : arg1 inside of arg2
         *   ::next : move arg towards 'end'
         *   ::next_or_jump : move arg towards 'end' or jump w/ new cached val
         *   ::in_window : arg between 'begin' and 'end'
         *   ::is_tradable : arg is outside 'begin'
         *                            best bids/asks after trade activity
         */
        template<bool Bidside, bool Redirect> friend struct detail::exec::core;

        /*
         * aon order execution helpers
         *   ::in_window : arg within cached range
         *   ::adjust_state_after_pull : adjust cached range after chain removed
         *   ::adjust_state_after_insert : adjust cached range after chain inserted
         *   ::overlapping : return aon orders that go passed inside limits
         */
        template<bool BidSide> friend struct detail::exec::aon;

        /*
         * limit order execution helpers
         *   ::adjust_state_after_insert : adjust internal pointers after
         *                                 order insert
         *   ::adjust_state_after_pull : adjust internal pointers after
         *                               order pull
         *   ::in_window : arg with cached range
         *   ::is_tradable : arg is at or outside of inside cached val
         */
        template<bool BidSide> friend struct detail::exec::limit;

        /*
         * stop order execution helpers
         *   ::adjust_state_after_insert : adjust internal pointers after
         *                                 order insert
         *   ::adjust_state_after_pull : adjust internal pointers after
         *                               order pull
         *   ::adjust_state_after_trigger : adjust internal pointers after
         *                                  stop is triggered
         */
        template<bool BuyStop> friend struct detail::exec::stop;


        /* handles the async/consumer side of the order queue */
        void
        _threaded_order_dispatcher();

        id_type
        _execute_external_order(const external_order_queue_elem& e);

        /* all order types go through here */
        void
        _insert_order(order_queue_elem& e);

        /* if we need immediate (partial/full) fill info for basic order type*/
        bool
        _inject_basic_order(const order_queue_elem& e, bool partial_ok);

        /* basic order types */
        template<side_of_trade side = side_of_trade::both>
        fill_type
        _route_basic_order(const order_queue_elem& e,
                           bool pass_conditions = false);

        /* advanced order types */
        void
        _route_advanced_order(order_queue_elem& e);

        template<bool BidSide>
        size_t
        _trade(plevel plev,
               id_type id,
               size_t size,
               const order_exec_cb_type& exec_cb);

        std::pair<size_t, bool>
        _hit_chain(plevel plev,
                   id_type id,
                   size_t size,
                   const order_exec_cb_type& exec_cb);

        std::pair<size_t, bool>
        _hit_aon_chain(aon_chain_type *achain,
                       plevel plev,
                       id_type id,
                       size_t size,
                       const order_exec_cb_type& exec_cb );

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
        _insert_OCO_order(order_queue_elem& e);

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

        void
        _insert_ALL_OR_NOTHING_order(const order_queue_elem& e);

        template<bool IsBuy>
        size_t
        _match_aon_orders_PRE_trade(const order_queue_elem& e, plevel p);

        template<bool IsBuy>
        void
        _match_aon_orders_POST_trade(const order_queue_elem& e, plevel p);

        /* internal insert orders once/if we have an id */
        template<bool BuyLimit>
        fill_type
        _insert_limit_order( const order_queue_elem& e,
                             bool pass_conditions = false );

        template<bool BuyMarket>
        void
        _insert_market_order(const order_queue_elem& e);

        template<bool BuyStop>
        void
        _insert_stop_order( const order_queue_elem& e,
                            bool pass_conditions = false );

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

        void
        _trailing_stop_insert(id_type id, bool is_buy);

        void
        _trailing_stop_erase(id_type id, bool is_buy);

        plevel
        _generate_trailing_stop(bool buy_stop, size_t nticks);

        plevel
        _generate_trailing_limit(bool buy_limit, size_t nticks);

        /* push order onto the external queue, BLOCK */
        id_type
        _push_external_order(order_type oty,
                             bool buy,
                             double limit, // TickPrices ??
                             double stop, // TickPrices ??
                             size_t size,
                             order_exec_cb_type cb,
                             const AdvancedOrderTicket& aot,
                             id_type id = 0);

        /*
         * push order onto the internal queue, DONT BLOCK - this can
         * only be called by by the order dispatcher thread.
         */
        void
        _push_internal_order(order_type oty,
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


        /* limit @ p can fill sz */
        template<bool IsBuy>
        std::pair<bool, size_t>
        _limit_is_fillable( plevel p, size_t sz, bool allow_partial );

        /* remove a particular order by id... */
        bool
        _pull_order(id_type id, bool pull_linked);

        template<typename ChainTy>
        bool
        _pull_order(id_type id, bool pull_linked);

        /* pull OCO (linked) order */
        template<typename ChainTy>
        void
        _pull_linked_order(typename ChainTy::value_type& bndl);

        const chain_iter_wrap&
        _from_cache(id_type id) const;

        chain_iter_wrap&
        _from_cache(id_type id);

        bool
        _in_cache(id_type id) const;

        bool
        _is_buy_order(plevel p, const limit_bndl& o) const;

        bool
        _is_buy_order(plevel p, const stop_bndl& o) const;

        /* generate order ids; don't worry about overflow */
        inline id_type
        _generate_id()
        { return ++_last_id; }

        /* 
        * calculate chain_size of orders at each price level
        * use depth ticks on each side of last
        */
        template<side_of_market Side, typename ChainTy = limit_chain_type>
        std::map<double, typename std::conditional<Side == side_of_market::both,
                         std::pair<size_t, side_of_market>, size_t>::type >
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
        AdvancedOrderTicket
        _bndl_to_aot(const _order_bndl& bndl) const;

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

        template<typename T>
        static constexpr long long
        bytes_offset(const T *l, const T *r)
        { return (reinterpret_cast<unsigned long long>(l) -
               reinterpret_cast<unsigned long long>(r)); }

        template<typename T>
        static constexpr T*
        bytes_add(T *ptr, long long offset)
        {
            using C = typename std::conditional<
                std::is_const<T>::value, const char*, char*>::type;
            return reinterpret_cast<T*>( reinterpret_cast<C>(ptr) + offset );
        }

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
        pull_order(id_type id);

        order_info
        get_order_info(id_type id) const;

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
        dump_internal_pointers(std::ostream& out = std::cout) const;

        void
        dump_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
               out, std::min(_low_buy_limit, _ask),
               std::max(_high_sell_limit, _bid), side_of_trade::both); }

        void
        dump_buy_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
               out, _low_buy_limit, _bid, side_of_trade::buy); }

        void
        dump_sell_limits(std::ostream& out = std::cout) const
        { _dump_orders<limit_chain_type>(
               out, _ask, _high_sell_limit, side_of_trade::sell); }

        void
        dump_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
               out, std::min(_low_buy_stop, _low_sell_stop),
               std::max(_high_buy_stop, _high_sell_stop), side_of_trade::both); }

        void
        dump_buy_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
               out, _low_buy_stop , _high_buy_stop, side_of_trade::buy); }

        void
        dump_sell_stops(std::ostream& out = std::cout) const
        { _dump_orders<stop_chain_type>(
               out, _low_sell_stop, _high_sell_stop, side_of_trade::sell); }

        std::map<double, size_t>
        bid_depth(size_t depth=8) const
        { return _market_depth<side_of_market::bid>(depth); }

        std::map<double,size_t>
        ask_depth(size_t depth=8) const
        { return _market_depth<side_of_market::ask>(depth); }

        std::map<double,std::pair<size_t, side_of_market>>
        market_depth(size_t depth=8) const
        { return _market_depth<side_of_market::both>(depth); }

        double
        bid_price() const;

        double
        ask_price() const;

        double
        last_price() const;

        double
        min_price() const;

        double
        max_price() const;

        size_t
        bid_size() const;

        size_t
        ask_size() const;

        size_t
        total_bid_size() const
        { return _total_depth<side_of_market::bid>(); }

        size_t
        total_ask_size() const
        { return _total_depth<side_of_market::ask>(); }

        size_t
        total_size() const
        { return _total_depth<side_of_market::both>(); }

        size_t
        last_size() const;

        unsigned long long
        volume() const;

        id_type
        last_id() const;

        const std::vector<timesale_entry_type>&
        time_and_sales() const;

        /* NEW - AON orders */
        std::map<double, std::pair<size_t,size_t>>
        aon_market_depth() const;

        size_t
        total_aon_bid_size() const;

        size_t
        total_aon_ask_size() const;

        size_t
        total_aon_size() const;

        void
        dump_aon_buy_limits(std::ostream& out = std::cout) const;

        void
        dump_aon_sell_limits(std::ostream& out = std::cout) const;

        void
        dump_aon_limits(std::ostream& out = std::cout) const;
    };

    /* (non-inline) definitions in tpp/orderbook/impl.tpp */
    template<typename TickRatio>
    class SimpleOrderbookImpl
            : SimpleOrderbookBase{
        /* manage instances created by factory proxy */
        static SOB_RESOURCE_MANAGER<FullInterface, ImplDeleter> rmanager;

        SimpleOrderbookImpl(const SimpleOrderbookImpl& sob) = delete;
        SimpleOrderbookImpl(SimpleOrderbookImpl&& sob) = delete;
        SimpleOrderbookImpl& operator=(const SimpleOrderbookImpl& sob) = delete;
        SimpleOrderbookImpl& operator=(SimpleOrderbookImpl&& sob) = delete;

        SimpleOrderbookImpl( TickPrice<TickRatio> min, size_t incr );
        ~SimpleOrderbookImpl() {}

        /* lowest price */
        TickPrice<TickRatio> _base;

        /* price-to-index and index-to-price utilities  */
        plevel /* NOT THREAD-SAFE */
        _ptoi(TickPrice<TickRatio> price) const;

        TickPrice<TickRatio> /* NOT THREAD-SAFE */
        _itop(plevel p) const;

        bool /* NOT THREAD-SAFE */
        _is_valid_price(double price) const;

        /* called by ManagementInterface to increase book size */
        void /* NOT THREAD-SAFE */
        _grow_book(TickPrice<TickRatio> min, size_t incr, bool at_beg);

    public:
        void
        grow_book_above(double new_max);

        void
        grow_book_below(double new_min);

        double
        tick_size() const
        { return tick_size_(); }

        double
        price_to_tick(double price) const
        { return price_to_tick_(price); }

        long long
        ticks_in_range(double lower, double upper) const
        { return ticks_in_range_(lower, upper); }

        long long
        ticks_in_range() const;

        bool
        is_valid_price(double price) const;

        static FullInterface*
        create(double min, double max)
        { return create( TickPrice<TickRatio>(min), TickPrice<TickRatio>(max) ); }

        static FullInterface*
        create( TickPrice<TickRatio> min, TickPrice<TickRatio> max );

        static void
        destroy(FullInterface *interface)
        { if( interface ) rmanager.remove(interface); }

        static void
        destroy_all()
        { rmanager.remove_all(); }

        static std::vector<FullInterface *>
        get_all()
        { return rmanager.get_all(); }

        static bool
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
        { return ( TickPrice<TickRatio>(upper)
                 - TickPrice<TickRatio>(lower) ).as_ticks(); }

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

using DefaultFactoryProxy = SimpleOrderbook::FactoryProxy<>;

template<typename TickRatio>
SOB_RESOURCE_MANAGER<FullInterface, SimpleOrderbook::ImplDeleter>
SimpleOrderbook::SimpleOrderbookImpl<TickRatio>::rmanager(
        typeid(TickRatio).name()
        );

struct order_info {
    order_type type;
    bool is_buy;
    double limit;
    double stop;
    size_t size;
    AdvancedOrderTicket advanced;

    inline operator bool()
    { return type != order_type::null; }

    order_info();

    order_info(order_type type,
               bool is_buy,
               double limit,
               double stop,
               size_t size,
               const AdvancedOrderTicket& advanced);

    order_info(const order_info& oi);
};

namespace detail{

struct sob_types {
    using sob_class = SimpleOrderbook::SimpleOrderbookBase;
    using plevel = sob_class::plevel;
    using limit_chain_type = sob_class::limit_chain_type;
    using stop_chain_type = sob_class::stop_chain_type;
    using aon_chain_type = sob_class::aon_chain_type;
    using stop_bndl = sob_class::stop_bndl;
    using limit_bndl = sob_class::limit_bndl;
    using aon_bndl = sob_class::aon_bndl;
    using order_queue_elem = sob_class::order_queue_elem;
    using _order_bndl = sob_class::_order_bndl;
    using OrderNotInCache = sob_class::OrderNotInCache;
    using chain_iter_wrap = sob_class::chain_iter_wrap;
};

} /* detail */

}; /* sob */

/* method definitions for SimpleOrderbookImpl */
#include "../src/orderbook/impl.tpp"

#endif /* JO_SOB_SIMPLEORDERBOOK */
