SimpleOrderbook is a collection of C++ objects, and a python interface, for matching real-time financial market order flow. The core module is implemented as a C++ class template, providing virtual interfaces. The Python-C API extension module sits on top - wrapping the core implementation objects - providing its own object-oriented interface.

To build/run without the python extension you'll need to write your own C/C++ that defines an entry(main); include simpleorderbook.hpp and marketmakers.hpp; compile with simpleorderbook.cpp and marketmaker.cpp; and link:

```g++ --std=c++11 -lpthread simpleorderbook.cpp marketmaker.cpp YOUR_OWN_CODE.cpp -o YOUR_OWN_CODE.out```

####++ Contents

- simpleorderbook.hpp / simpleorderbook.tpp :: the core code for the orderbook
- interfaces.hpp :: virtual interfaces to access the orderbook
- marketmaker.hpp / marketmaker.cpp :: 'autonomous' agents the provide liquidity to the orderbook
- python/ :: all the C/C++ code (and the setup.py script) for the python extension module

#####++ Requirements 
- c++11(c++0x) compiler support (or a willingness to backport)
- knowledge of basic market order types, terminology, concepts etc.

#####++ Features 
- market, limit, stop-market, and stop-limit orders that trigger callbacks when executed
- cancel/replace orders by ID
- query market state(bid size, volume etc.), dump orders to stdout, view Time & Sales 
- high-speed order-matching/execution
- template objects that can be instantiated by increment size and max memory usage allowed 
- Market Maker objects that operate as autonomous agents 'inside' the Orderbook
- customizable market parameters( price range, # of market makers etc.)
- (possibly) more advanced order types

#####++ Licensing & Warranty

*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
