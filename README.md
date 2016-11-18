## SimpleOrderbook 
- - -
Collection of C++ objects, and a python interface, for matching real-time financial market order flow. The core module is implemented as a C++ class template, providing virtual interfaces. The Python-C API extension module sits on top - wrapping the core implementation objects - providing its own object-oriented interface.

#### Requirements 
- c++11 compiler support (or a willingness to backport)
- knowledge of basic market order types, terminology, concepts etc.

#### Features 
- market, limit, stop-market, and stop-limit orders that trigger callbacks when executed
- cancel/replace orders by ID
- query market state(bid size, volume etc.), dump orders to stdout, view Time & Sales 
- high-speed order-matching/execution
- Market Maker objects that operate as autonomous agents 'inside' the Orderbook

#### Build / Install / Run

- **python**

        user@host:/usr/local/SimpleOrderbook/python$ python setup.py install
        user@host:/usr/local/SimpleOrderbook/python$ python
        >>> import simpleorderbook      

- **C++** 

        user@host:/usr/local/SimpleOrderbook$ g++ --std=c++11 -lpthread simpleorderbook.cpp marketmaker.cpp example_code.cpp -o example_code.out
        user@host:/usr/local/SimpleOrderbook$ ./example_code.out  
- - -
    
        // example_code.cpp

        #include "simpleoderbook.hpp"
        #include "marketmaker.hpp"

        class MyCustomMarketMaker
                : public NativeLayer::MarketMaker{
            // define
        };

        void 
        execution_callback(NativeLayer::callback_msg msg, 
                           NativeLayer::id_type id,
                           NativeLayer::pirce_type price,
                           NativeLayer::size_type size)
        {
            // define
        }

        void 
        insert_orders(NativeLayer::SimpleOrderbook::FullInterface& orderbook)
        {
            /* buy 50 @ 49.92 or better */
            auto id1 = orderbook.insert_limit_order(true, 49.92, 50, execution_callback);
            /* sell 10 @ market */
            auto id2 = orderbook.insert_market_order(false, 10, execution_callback);
            //...
        }

        void
        print_inside_market(NativeLayer::SimpleOrderbook::QueryInterface& orderbook)
        {
            std::cout<< "BID: " << orderbook.bid_size() 
                     << " @ " << orderbook.bid_price() << std::endl;
            std::cout<< "ASK: " << orderbook.ask_size() 
                     << " @ " << orderbook.ask_price() << std::endl;
            std::cout<< "LAST: " << orderbook.last_size() 
                     << " @ " << orderbook.last_price() << std::endl;
        }

        int
        main(int argc, char* argv[])
        {
            using namespace NativeLayer;

            typedef std::ratio<1,4> quarter_tick;

            /* .25 tick size; default (1 GB) max memory; 
               start price: 50.00; minimum price: 1.00, maximum price: 100.00 */
            SimpleOrderbook::SimpleOrderbook<quarter_tick> orderbook(50.00, 1.00, 100.00);

            /* instantiate (1) 'simple' mm;
               trades 100 at a time, limited to 100000 long or short */
            MarketMaker_Simple1 simple_market_maker(100,100000);

            /* instantiate a vector of (3) 'random' mms  
               each trades 10 to 200 at a time, limited to 100000 long or short */
            auto random_market_makers_3 = MarketMaker_Random::Factory(3,10,200,100000);

            /* instantiate (1) of our own market makers */
            MyCustomMarketMaker custom_market_maker(...);

            /* move market makers into the orderbook */
            orderbook.add_market_maker( std::move(simple_market_maker) );
            orderbook.add_market_makers( std::move(random_market_makers_3) );
            orderbook.add_market_maker( std::move(custom_market_maker) );
            /* NOTE: we moved the MMs and *shouldn't* access them after this point*/

            /* access the interface(s) */
            insert_orders(orderbook);            
            print_inside_market(orderbook);
       
            //...
            
            return 0;
        }   
    

#### Contents
- simpleorderbook.hpp / simpleorderbook.tpp :: the core code for the orderbook
- interfaces.hpp :: virtual interfaces to access the orderbook
- marketmaker.hpp / marketmaker.cpp :: 'autonomous' agents the provide liquidity to the orderbook
- python/ :: all the C/C++ code (and the setup.py script) for the python extension module

#### Licensing & Warranty
*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
