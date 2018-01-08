## SimpleOrderbook 
- - -

An experimental C++(11) financial-market orderbook and matching engine w/ a Python extension module.

\* **v0.3 is currently undergoing a complete refactoring** 


#### Features 

- market, limit, stop-market, and stop-limit orders that trigger callbacks when executed
- cancel/replace orders by ID
- query market state(bid size, volume etc.), dump orders to stdout, view Time & Sales 
- access via a CPython extension module
- orderbook methods are accessed via a number of (virtual) interfaces
- orderbook objects are allocated/managed by a 2-level static resource manager
- sacrifices space for speed (e.g using static price levels, pre-allocation of internal objects)


#### Getting Started

- **C++** 

        user@host:/usr/local/SimpleOrderbook$ g++ --std=c++11 -lpthread simpleorderbook.cpp example_code.cpp -o example_code.out
        user@host:/usr/local/SimpleOrderbook$ ./example_code.out  

- **python**

        user@host:/usr/local/SimpleOrderbook/python$ python setup.py install
        user@host:/usr/local/SimpleOrderbook/python$ python
        >>> import simpleorderbook           

#### Examples
    
        // example_code.cpp

        #include "simpleoderbook.hpp"

        void 
        execution_callback(sob::callback_msg msg, 
                           sob::id_type id,
                           sob::pirce_type price,
                           sob::size_type size);

        void 
        insert_orders(sob::FullInterface *orderbook);

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
             *     SimpleOrderbook::SimpleOrderbookImpl< std::ratio<1,4>, 1024 * 1024 * 8>
             *
             * - uses the default factory 'create' function via (double,double) constructor
             * - with .25 price intervals
             * - and a max 8MB of pre-allocated (internal) storage (see source comments)
             * 
             * NOTICE the use of the copy constructor. Factory Proxies RESTRICT 
             * DEFAULT CONSTRUCTION to insure non-null function pointer fields
             */
            const size_t MAX_MEM = 1024 * 1024 * 8;
            SimpleOrderbook::FactoryProxy<> qt8_def_proxy( 
                SimpleOrderbook::BuildFactoryProxy<quarter_tick, MAX_MEM>()
            );

           /*
            * This approach(assuming we use the default FactoryProxy) 
            * provides factory interfaces - for each and ANY type of 
            * orderbook - all of the same type; allowing for:
            */           
            std::map<std::string, SimpleOrderbook::FactoryProxy<>> 
            my_factory_proxies = { 
                {"QT", qt8_def_proxy},
                {"TT", SimpleOrderbook::BuildFactoryProxy<tenth_tick, MAX_MEM * 4>()}
                {"HT", SimpleOrderbook::BuildFactoryProxy<std::ratio<1,2>, MAX_MEM >()}
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

            /* 
             * WHEN DONE...
             */
          
            /* use the proxy to destroy the orderbook it created */
            bool success1 = my_factory_proxies.at("QT").destroy(orderbook);

            /* (or) use the global version to destroy ANY orderbook */
            bool success2 = SimpleOrderbook::Destroy(orderbook);

            /* 
             * NOTE: orderbook should only be destroyed once:
             *       success1 == true, success2 == false
             */  
       
            /* use the proxy to destroy all the orderbooks it created */
            my_factory_proxies.at("QT").destroy_all()

            /* (or) use the global version to destroy ALL orderbooks */
            SimpleOrderbook::DestroyAll();

            //...
            
            return 0;
        }   

        void 
        execution_callback(sob::callback_msg msg, 
                           sob::id_type id,
                           sob::pirce_type price,
                           sob::size_type size)
        {
            // define
        }

        void 
        insert_orders(sob::FullInterface *orderbook)
        {
            /* buy 50 @ 49.75 or better */
            auto id1 = orderbook->insert_limit_order(true, 49.75, 50, execution_callback);
            /* sell 10 @ market */
            auto id2 = orderbook->insert_market_order(false, 10, execution_callback);
            //...
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


#### Licensing & Warranty
*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
