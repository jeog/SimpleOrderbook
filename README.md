SimpleOrderbook is an interactive back-end for handling real-time financial market order flow. It's in early development, has undergone almost no testing, and is currently only intended for animating market models and simulations.

The core module is implemented as a C++ class template, providing low-level (pure virtual) interfaces. The Python-C API extension module sits on top - wrapping the core implementation objects - providing its own object-oriented interface.

#####++ Features 
- market, limit, stop-market, and stop-limit orders that trigger callbacks when executed
- cancel/replace orders by ID
- query market state(bid size, volume etc.), dump orders to stdout, view Time & Sales 
- high-speed order-matching/execution
- template objects that be instantiated by increment size and max memory usage allowed
- MarketMaker objects that operate as autonomous agents 'inside' the Orderbook
- customizable market parameters( price range, # of market makers etc.)
- (possibly) more advanced order types

#####++ Requirements
- C++ or Python  
- c++11(c++0x) compiler support (or a willingness to backport)
- knowledge of basic market order types, terminology, concepts etc.

#####++ Licensing & Warranty

*SimpleOrderbook is released under the GNU General Public License(GPL); a copy (LICENSE.txt) should be included. If not, see http://www.gnu.org/licenses. The author reserves the right to issue current and/or future versions of SimpleOrderbook under other licensing agreements. Any party that wishes to use SimpleOrderbook, in whole or in part, in any way not explicitly stipulated by the GPL, is thereby required to obtain a separate license from the author. The author reserves all other rights.*

*This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*
