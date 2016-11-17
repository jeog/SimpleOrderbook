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
##### python
```
user@host:/usr/local/SimpleOrderbook/python$ python setup.py install
user@host:/usr/local/SimpleOrderbook/python$ python
>>> import simpleorderbook
```

##### C++
- define an entry(main); include simpleorderbook.hpp and marketmakers.hpp 
- instantiate a NativeLayer::SimpleOrderbook::SimpleOrderbook\<TickSize,MaxMemory\> or use one of the typedefs from types.hpp 
- instantiate a NativeLayer::MarketMaker, one of its derived classes, or derive your own; add them to the SimpleOrderbook
```
user@host:/usr/local/SimpleOrderbook$ g++ --std=c++11 -lpthread simpleorderbook.cpp marketmaker.cpp YOUR_OWN_CODE.cpp -o YOUR_OWN_CODE.out
user@host:/usr/local/SimpleOrderbook$ ./YOUR_OWN_CODE.out
```

#### Contents
- simpleorderbook.hpp / simpleorderbook.tpp :: the core code for the orderbook
- interfaces.hpp :: virtual interfaces to access the orderbook
- marketmaker.hpp / marketmaker.cpp :: 'autonomous' agents the provide liquidity to the orderbook
- python/ :: all the C/C++ code (and the setup.py script) for the python extension module

#### Licensing & Warranty
*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
