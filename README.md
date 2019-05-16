## SimpleOrderbook v0.6
- - -

SimpleOrderbook is a C++(11) financial market orderbook and matching engine with a Python extension module.

#### Features 

- market, limit, stop-market, and stop-limit order types
- advanced orders/conditions: 
    - one-cancels-other (OCO) 
    - one-triggers-other (OTO)
    - fill-or-kill (FOK)
    - bracket
    - trailing stop 
    - bracket /w trailing stop
    - all-or-none (AON) ***(NEW, C++ interface added in V0.6, not stable)***
- advanced condition triggers:
    - fill-partial 
    - fill-full 
    - fill-n-percent ***(not available yet)***
- cancel/replace orders by ID
- callbacks on order execution/cancelation/advanced triggers etc.
- synchronous & asynchronous order insertion/callback ***NEW***
- query market state(bid size, volume etc.), dump orders to stdout, view Time & Sales 
- extensible backend resource management(global and type-specific) via factory proxies 
- tick sizing/rounding/math handled implicity by TickPrice\<std::ratio\> objects
- pre-allocation of (some) internals during construction to reduce runtime overhead
- (manually) grow orderbooks as necessary
- access via a CPython extension module

#### Design

The 'spine' of the orderbook is a vector which allows random access using simple pointer/index math internally.

The vector is initialized to user-requested size when the book is created but can be grown manually. We simply let the STL implementation do its thing, get the new base address, and adjust the internal pointers by the offset.

The vector contains pairs of doubly-linked lists (stop and limit 'chains') so order insert/execution is O(1) for limit/market orders (see below).

Orders are referenced by ID #s that are generated sequentially and cached - with their respective price level and chain iterator - in a hash table, allowing for collision-free O(1) lookup from the cache to pull and replace orders.

See 'Performance Tests' section below for run times of standard orders. 

##### MultiThreading 

All order matching and execution is done on a separate 'dispatcher/execution' thread via a thread-safe queue. Each time an order is popped from the queue a lock is acquired and the order is processed, as well as ***all contingent orders*** (e.g a stop is triggered -> a new limit is inserted -> a trade occurs -> this trade triggers an OTO -> etc. etc.) before releasing the lock. We'll refer to this as the 'execution window' as it's an important concept for understanding synchronous vs asynchronous insert and callback.

To access the state of the orderbook(e.g bid_price, market_depth) the same lock is acquired so the caller can be assured the most recent execution window has completed and the book is in a 'static' state.

##### Synchronous Access

Standard insert/replace/pull orders BLOCK until the execution window is closed and return either:
1. a valid order ID for 'insert' or 'replace'
2. '0' for an error during 'replace'
3. true/false for success of 'pull'

Any callback events that took place inside the window are not executed until AFTER the window closes, but BEFORE the function returns. These callbacks are all executed from the ***thread of the caller*** in the order they occured (not from the dispatcher/execution thread). Callbacks from orders inserted previously will also be executed in the CURRENT calling thread.

*If the synchronous interface is used from multiple threads there's no guarantee that the callbacks from an earlier window will occur before those of a later window OR order will be maintained.*

##### Asynchronous Access 

Insert/replace/pull orders with an '_async' suffix return IMMEDIATELY, with a ```std::future<id_type>``` object. When the execution window is closed the ```.get()``` method will return either:
1. a valid order ID for 'insert' or 'replace'
2. '0' for an error during 'replace' or 'pull'
3. '1' for a successful 'pull'

It can also throw an exception. ( ```.wait()```  is similar but doesn't return anything and will not throw.) Any callback events that take place inside the window are immediately pushed to and executed from a ***separate callback thread***. The only guarantee is that the order of callbacks is maintained, accross windows. It's important to keep in mind that just because the future object's ```.get()``` or ```.wait()``` method returns doesn't mean the callbacks from that window will have occurred yet.

*Callbacks never occur from the dispatcher/execution thread.*


##### All-Or-None Functionality

Recently added 'all-or-none' orders use a combination of traditional limit chains and separate buy and sell ('aon') chains that allow for limit buys to be stored at or above the ask and limit sells at or below the bid. This creates a relatively high level of complexity behind the scenes that won't prove stable for some time. 

The implementation currently caches range bounds and iterates naively; it requires a fair amount of 'look-ahead' for both new and old orders; and re-checking of outstaning AON orders on each new entry - all of which will cause performance issues if these order types are used extensively.

*To avoid the performance and stability issues with AON orders you can revert to v0.5 which can be found on the appropriately named branch.*

##### Price-Mediation

Options and functionality for determining 'fair' prices on crossed orders, particularly when large AONs are used, will be added in the future.


#### Build

The only necessary thing to build is the SimpleOrderbook static library. The functional and performance tests are optional.

##### Linux/Unix/OS X
```
user@host:/usr/local/SimpleOrderbook$ make release #release build of lib 
user@host:/usr/local/SimpleOrderbook$ make debug #debug build of lib
user@host:/usr/local/SimpleOrderbook$ make functional-test #debug build of functional tests (depends on lib)
user@host:/usr/local/SimpleOrderbook$ make performance-test #release build of performance tests (depends on lib)
user@host:/usr/local/SimpleOrderbook$ make all #all of the above
```

##### Windows

Use the VisualStudio solution in /vsbuild. You only need to build the SimpleOrderbook project but it's recommended to build the entire solution:

1. file -> open -> Project/Solution -> SimpleOrderbook/vsbuild/vsbuild.sln
2. select configuration to build (Release vs. Debug, Win32 vs. x64): 
    - Build -> Configuration Manager
    - Debug recommended for FunctionalTest (includes internal asserts)
    - Release recommended for PerformanceTest   
3. Build -> Build Solution


#### Functional Tests(Optional)

- pass a valid path to send additional output to a file, '-' to stdout (defaults to neither)
- if anything fails please report: 
    1. specific test name 
    2. error code returned
    3. system/architecture used
    4. any custom compiler options or changes to source

```
    user@host:/usr/local/SimpleOrderbook$ make functional-test
    user@host:/usr/local/SimpleOrderbook$ bin/debug/FunctionalTest ftest.out

    *** BEGIN SIMPLEORDERBOOK FUNCTIONAL TESTS ***
    ** Test_tick_price<1/4> ** SUCCESS
    ** TEST_basic_orders_1 - 1/4 - 0-10 ** SUCCESS
    ** TEST_basic_orders_1 - 1/4 - 0.25-1000 ** SUCCESS
    ** TEST_basic_orders_1 - 1/4 - 1000-1100 ** SUCCESS
    ...
    ** TEST_advanced_TRAILING_BRACKET_5 - 1/10000 - 9.999-10.01 ** SUCCESS
    ** TEST_advanced_TRAILING_BRACKET_5 - 1/1000000 - 2e-06-0.0001 ** SUCCESS
    *** END SIMPLEORDERBOOK FUNCTIONAL TESTS *** 

```

Windows: FunctionalTest.exe in bin/debug/{platform}/ if you followed build recomendations above.


#### Getting Started

##### C++

Include simpleorderbook.hpp and link w/ library.
```
    user@host:/usr/local/SimpleOrderbook$ make release
    user@host:/usr/local/SimpleOrderbook$ g++ --std=c++11 -Iinclude -lpthread samples/example_code.cpp bin/release/libSimpleOrderbook.a -o example_code.out
    user@host:/usr/local/SimpleOrderbook$ ./example_code.out  
```

##### Python

Run the setup script.
```
    user@host:/usr/local/SimpleOrderbook$ make release
    user@host:/usr/local/SimpleOrderbook/python$ python setup.py install
    user@host:/usr/local/SimpleOrderbook/python$ python
    >>> import simpleorderbook           
```

#### Debug

##### C++

    user@host:/usr/local/SimpleOrderbook$ make debug
    user@host:/usr/local/SimpleOrderbook$ g++ -g -O0 --std=c++11 -Iinclude -lpthread samples/example_code.cpp bin/debug/libSimpleOrderbook.a -o example_code.out
    user@host:/usr/local/SimpleOrderbook$ gdb example_code.out  

##### Python/C

    TERMINAL #1
    user@host:/usr/local/SimpleOrderbook$ make debug 
    user@host:/usr/local/SimpleOrderbook/python$ python setup_debug.py install
    user@host:/usr/local/SimpleOrderbook$ python 
    >>> import simpleorderbook as sob
    >>> import os
    >>> os.getpid()
    33333
    ...
    >>> ob = sob.SimpleOrderbook(sob.SOB_QUARTER_TICK,1,100)

    TERMINAL #2
    user@host:/usr/local/SimpleOrderbook$ gdb -p 33333
    (gdb)b sob::SimpleOrderbook::SimpleOrderbookImpl<std::ratio<1l,4l>>::create  
    (gdb)c
    Continuing.
    ...


#### Performance Tests

- use orderbooks of 1/100 TickRatio of varying sizes (nticks)
- average 9 separate runs of each test using 3 threads on i7-8700 cpu
- output is TOTAL run time, NOT per order
- some of the tests take a while (edit test/performance.cpp to change)
```    
    user@host:/usr/local/SimpleOrderbook$ make performance-test
    user@host:/usr/local/SimpleOrderbook$ bin/release/PerformanceTest
    ...
```

##### limit insert/execute tests

- prices are distributed normally around the mid-point w/ a SD 5% of the range
- order sizes are distributed log-normally * 200 (Gibrat's distribution)
- buy/sell condition from a simple .50 bernoulli 

```
                              total run-time (seconds)

                                 number of orders                            

                     | 1000      10000     100000    1000000   
           ----------|-----------------------------------------
           1000      | 0.005626  0.052285  0.521364  5.279030  
book size  10000     | 0.046065  0.053382  0.530615  5.318732 
(nticks)   100000    | 0.045500  0.055734  0.547417  5.613535 
           1000000   | 0.056053  0.117140  1.051951  10.459269 
```

##### limit-market-stop insert/execute tests 

- n orders: 40% limits, 20% markets, 20% stops, 20% stop-limits
- prices are distributed normally around the mid-point w/ a SD 5% of the range
- order sizes are distributed log-normally * 200 (Gibrat's distribution)
- buy/sell condition from a simple .50 bernoulli 

```
                               total run-time (seconds)

                                  number of orders

                     | 1000      10000     100000    1000000   
           ----------|-----------------------------------------
           1000      | 0.049306  0.057941  0.557834  5.652893  
book size  10000     | 0.008065  0.076338  0.725552  7.315030 
(nticks)   100000    | 0.036896  0.324345  3.046376  30.438185 
           1000000   | 0.585892  5.270054  53.219992 528.671663


```

##### pull tests

- n orders are inserted, ids are stored
    - 50% limits, 25% stops, 25% stop-limits
    - prices are distributed normally around the mid-point w/ a SD 5% of the range
    - order sizes are distributed log-normally * 200 (Gibrat's distribution) 
    - buy/sell for limits is dependent on price relative to mid-point (NO TRADES)
    - buy/sell condition for stops from a simple .50 benoulli
- ids are randomly shuffled
- loop through ids and pull each order

```
                              total run-time (seconds)

                                 number of orders

                     | 1000      10000     100000    1000000   
           ----------|-----------------------------------------
           1000      | 0.005163  0.049501  0.497251  5.101262  
book size  10000     | 0.005250  0.049881  0.506277  5.140158 
(nticks)   100000    | 0.005432  0.048897  0.504907  5.177959 
           1000000   | 0.011626  0.054489  0.509403  5.195578  
```

##### replace tests

- n orders are inserted, ids are stored
    - 50% limits, 25% stops, 25% stop-limits
    - prices are distributed normally around the mid-point w/ a SD 5% of the range
    - order sizes are distributed log-normally * 200 (Gibrat's distribution) 
    - buy/sell for limits is dependent on price relative to mid-point (NO TRADES)
    - buy/sell condition for stops from a simple .50 benoulli
- ids are randomly shuffled
- prices, sizes, order-types, and stop buy/sell conditions are randomly shuffled
- loop through ids and replace each order w/ a new one using shuffled paramaters

```
                              total run-time (seconds)

                                 number of orders

                     | 1000      10000     100000    1000000   
           ----------|-----------------------------------------
           1000      | 0.010188  0.097775  0.975168  9.725051  
book size  10000     | 0.010047  0.094908  0.980188  9.829115 
(nticks)   100000    | 0.009804  0.099634  0.965952  9.847435 
           1000000   | 0.009742  0.101409  0.988162  9.846411  
 
```


#### Examples
 
        // example_code.cpp

        #include <unordered_map>
        #include "simpleorderbook.hpp"

        std::unordered_map<sob::id_type, sob::id_type> advanced_ids;

        void 
        execution_callback(sob::callback_msg msg, 
                           sob::id_type id1,
                           sob::id_type id2,
                           double price,
                           size_t size);

        void 
        insert_orders(sob::FullInterface *orderbook);

        void 
        insert_advanced_orders(sob::FullInterface *orderbook);

        void
        print_inside_market(sob::QueryInterface *orderbook);

        int
        main(int argc, char* argv[])
        {
            using namespace sob;

            /* 
             * First, we need to create a factory proxy. To support different 
             * orderbook types and different constructor types, in a single factory, 
             * we provide different factory interfaces(proxies).
             *
             * The following will be used for managing orderbooks of (implementation) type:
             *     SimpleOrderbook::SimpleOrderbookImpl< std::ratio<1,4> >
             *
             * - uses the default factory 'create' function via (double,double) constructor
             * - with .25 price intervals       
             * 
             * Proxies (implicitly) restrict default construction and assignment
             */         
            SimpleOrderbook::FactoryProxy<> qt_def_proxy = 
                SimpleOrderbook::BuildFactoryProxy<quarter_tick>();            

           /*
            * This approach(assuming we use the default FactoryProxy) 
            * provides factory interfaces - for each and ANY type of 
            * orderbook - all of the same type; allowing for:
            */           
            std::map<std::string, SimpleOrderbook::FactoryProxy<>> 
            my_factory_proxies = { 
                {"QT", qt_def_proxy},
                {"TT", SimpleOrderbook::BuildFactoryProxy<tenth_tick>()},
                {"HT", SimpleOrderbook::BuildFactoryProxy<std::ratio<1,2>>()}
            };

            /*  
             * Use the factory proxy to create an orderbook that operates 
             * between .25 and 100.00 in .25 increments, returning a pointer
             * to its full interface 
             *
             * NOTE: .create() is built to throw: logic and runtime errors (handle accordingly)
             */ 
            FullInterface *orderbook = my_factory_proxies.at("QT").create(.25, 100.00);                  
            if( !orderbook ){
                // error (this *shouldn't* happen)
                return 1;
            }

            /* 
             * IMPORTANT: internally, each orderbook is managed by a 'resource manager' 
             * behind each proxy AND a global one for ALL proxies/orderbooks. This 
             * allows for the use of that type's proxy member functions OR the global
             * static methods of the SimpleOrderbook container class. (see below)
             */

            /* check if orderbook is being managed by ANY proxy */
            if( !SimpleOrderbook::IsManaged(orderbook) )
            {
                /* get orderbooks being managed by ALL proxies */
                std::vector<FullInterface*> actives = SimpleOrderbook::GetAll();

                std::cerr<< "error: active orderbooks for ALL proxies" << std::endl;
                for( FullInterface *i : actives ){
                    std::cerr<< std::hex << reinterpret_cast<void*>(i) << std::endl;
                }
                return 1;
            }       

            /* check if orderbook is being managed by THIS proxy */
            if( !my_factory_proxies.at("QT").is_managed(orderbook) )
            {
                /* get all orderbooks being managed by THIS proxy */
                std::vector<FullInterface*> actives = my_factory_proxies.at("QT").get_all();

                std::cerr<< "error: active orderbooks for 'QT' proxy" << std::endl;
                for( FullInterface *i : actives ){
                    std::cerr<< std::hex << reinterpret_cast<void*>(i) << std::endl;
                }
                return 1;
            }    

            /* use the full interface (defined below) */
            insert_orders(orderbook);            

            /* use the query interface (defined below) */
            print_inside_market(orderbook);

            /* increase the size of the orderbook */
            dynamic_cast<ManagementInterface*>(orderbook)->grow_book_above(150);

            if( orderbook->min_price() != .25 
                || orderbook->max_price() != 150.00
                || orderbook->tick_size() != .25
                || orderbook->price_to_tick(150.12) != 150.00
                || orderbook->price_to_tick(150.13) != 150.25
                || orderbook->is_valid_price(150.13) != false
                || orderbook->ticks_in_range(.25, 150) != 599 )
            {
                std::cerr<< "bad orderbook" << std::endl;
                return 1;
            }

            /* use advanced orders (IN DEVELOPEMENT) */
            insert_advanced_orders(orderbook);

            /* 
             * WHEN DONE...
             */
          
            /* use the proxy to destroy the orderbook it created */
            my_factory_proxies.at("QT").destroy(orderbook);

            /* (or) use the global version to destroy ANY orderbook 
               NOTE: orderbook should only be destroyed once (no-op in this case) */
            SimpleOrderbook::Destroy(orderbook);

            /* use the proxy to destroy all the orderbooks it created */
            my_factory_proxies.at("QT").destroy_all();

            /* (or) use the global version to destroy ALL orderbooks */
            SimpleOrderbook::DestroyAll();

            //...
            
            return 0;
        }   

        void 
        execution_callback(sob::callback_msg msg, 
                           sob::id_type id1,
                           sob::id_type id2,
                           double price,
                           size_t size)
        {
            /* if we use OCO order need to be aware of potential ID# change on trigger */
            if( msg == sob::callback_msg::trigger_OCO ){
                std::cout<< "order #" << id1 << " is now #" << id2 << std::endl;
                advanced_ids[id1] = id2;
            }
            std::cout<< msg << " " << id1 << " " << id2 << " " 
                     << price << " " << size << std::endl;
            // define
        }

        void 
        insert_orders(sob::FullInterface *orderbook)
        {
            /* buy 50 @ 49.75 or better */
            sob::id_type id1 = orderbook->insert_limit_order(true, 49.75, 50, execution_callback);
            /* sell 10 @ market */
            sob::id_type id2 = orderbook->insert_market_order(false, 10, execution_callback);

            /* pull orders */
            std::cout<< "pull order #1 (should be true): " << std::boolalpha 
                     << orderbook->pull_order(id1) << std::endl;
            std::cout<< "pull order #2 (should be false): " 
                     << orderbook->pull_order(id2) << std::endl;
        }

        void 
        insert_advanced_orders(sob::FullInterface *orderbook)
        {
            /* create a OCO (one-cancels-other) buy-limit/sell-limit order */

            /* first create an AdvancedOrderTicket */
            auto aot = sob::AdvancedOrderTicketOCO::build_limit(false, 50.00, 100, 
                            sob::condition_trigger::fill_partial);

            /* then insert it to the standard interface 
               NOTE: it will call back when the condition is triggered,
                     the new order id will be in field 'id2' */
            sob::id_type id = orderbook->insert_limit_order(true, 49.50, 100, execution_callback, aot);
            advanced_ids[id] = id;
            std::cout<< "ORDER #" << id << ": " << orderbook->get_order_info(id) << std::endl;

            /* if either order fills the other is canceled (and ID# may have changed)*/
            orderbook->insert_market_order(true, 50);
            sob::order_info oi = orderbook->get_order_info(advanced_ids[id]);
            std::cout<< "ORDER #" << advanced_ids[id] << ": " << oi << std::endl;

            orderbook->dump_buy_limits();
            orderbook->dump_sell_limits();
        }

        void
        print_inside_market(sob::QueryInterface *orderbook)
        {
            std::cout<< "BID: " << orderbook->bid_size() 
                     << " @ " << orderbook->bid_price() << std::endl;
            std::cout<< "ASK: " << orderbook->ask_size() 
                     << " @ " << orderbook->ask_price() << std::endl;
            std::cout<< "LAST: " << orderbook->last_size() 
                     << " @ " << orderbook->last_price() << std::endl;
        }

<br>
    
        // python

        >>> import simpleorderbook as sob
        >>> ob = sob.SimpleOrderbook(sob.SOB_QUARTER_TICK, .25, 100) 
        >>> print("%f to %f by %f" % (ob.min_price(), ob.max_price(), ob.tick_size()))
        0.250000 to 100.000000 by 0.250000
        >>> cb = lambda a,b,c,d,e: print("+ msg:%i id_old:%i id_new:%i price:%f size:%i" % (a,b,c,d,e))
        >>> ob.buy_limit(limit=20.0, size=100, callback=cb) 
        1
        >>> ob.buy_limit(20.25, 100, cb)
        2
        >>> ob.buy_limit(21.0, 100, cb)
        3
        >>> ob.bid_depth(10)
        {20.0: 100, 20.25: 100, 21.0: 100}
        >>> ob.bid_size()
        100
        >>> ob.total_bid_size()
        300
        >>> ob.sell_stop(stop=21.0, size=50) 
        4
        >>> ob.sell_stop_limit(stop=21.0, limit=20.75, size=100, callback=cb) 
        5
        >>> ob.dump_sell_stops()
        *** (sell) stops ***
        21 <S 50 @ MKT #4>  <S 100 @ 20.750000 #5> 
        >>> ob.dump_buy_limits()
        *** (buy) limits ***
        21 <100 #3> 
        20.25 <100 #2> 
        20 <100 #1> 
        >>> ob.sell_market(50) 
        + msg:1 id_old:3 id_new:3 price:21.0 size:50 # callback from order #3 (1 == FILL) 
        + msg:2 id_old:5 id_new:8 price:20.75 size:100 # callback from order #5 (2 == STOP-TO-LIMIT)
        + msg:1 id_old:3 id_new:3 price:21.0 size:50 # callback from order #3 (1 == FILL)
        6
        >>> for ts in ob.time_and_sales():
        ...  ts
        ... 
        ('Sat Jan 13 18:51:31 2018', 21.0, 50)
        ('Sat Jan 13 18:51:31 2018', 21.0, 50)
        >>> ob.market_depth(10)
        {20.0: (100, 1), 20.25: (100, 1), 20.75: (100, -1)}  ## 1 == SIDE_BID, -1 == SIDE_ASK
        >>> ob.dump_buy_limits()
        *** (buy) limits ***
        20.25 <100 #2> 
        20 <100 #1> 
        >>> ob.pull_order(id=2)
        + msg:0 id_old:2 id_new:2 price:0.0 size:0 # callback from order #2 (0 == CANCEL)
        True
        >>> ob.replace_with_buy_limit(id=1, limit=20.50, size=500) # callback=None
        + msg:0 id_old:1 id_new:1 price:0.0 size:0 # callback from order #1 (0 == CANCEL)
        9
        >>> ob.dump_buy_limits()
        *** (buy) limits ***
        20.5 <500 #9> 

#### Licensing & Warranty
*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
