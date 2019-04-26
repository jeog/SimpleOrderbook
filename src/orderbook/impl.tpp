/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include <iomanip>
#include <iterator>
#include <climits>

/*
 * INCLUDED at the bottom of simpleorderbook.hpp to define the methods
 * of templace class SimpleOrderbook::SimpleOrderbookImpl
 */

#define SOB_TEMPLATE template<typename TickRatio>
#define SOB_CLASS SimpleOrderbook::SimpleOrderbookImpl<TickRatio>

namespace sob{

SOB_TEMPLATE
SOB_CLASS::SimpleOrderbookImpl(TickPrice<TickRatio> min, size_t incr)
    :
        SimpleOrderbookBase(
                incr,
                [this](SimpleOrderbookImpl::plevel p)->double{
                    return this->_itop(p);
                },
                [this](double price)-> SimpleOrderbookImpl::plevel{
                    return this->_ptoi(TickPrice<TickRatio>(price) );
                }
                ),
        _base(min)
    {
    }

SOB_TEMPLATE
void
SOB_CLASS::grow_book_above(double new_max)
{
    auto diff = TickPrice<TickRatio>(new_max) - max_price();

    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_max too far from old max to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(_base, incr, false);
    }
}


SOB_TEMPLATE
void
SOB_CLASS::grow_book_below(double new_min)
{
    if( _base == 1 ){ // can't go any lower
        return;
    }

    TickPrice<TickRatio> new_base(new_min);
    if( new_base < 1 ){
        new_base = TickPrice<TickRatio>(1);
    }

    auto diff = _base - new_base;
    if( diff > std::numeric_limits<long>::max() ){
        throw std::invalid_argument("new_min too far from old min to grow");
    }
    if( diff > 0 ){
        size_t incr = static_cast<size_t>(diff.as_ticks());
        _grow_book(new_base, incr, true);
    }
}


SOB_TEMPLATE
bool
SOB_CLASS::is_valid_price(double price) const
{
    long long offset = (TickPrice<TickRatio>(price) - _base).as_ticks();
    plevel p = _beg + offset;
    return (p >= _beg && p < _end);
}


SOB_TEMPLATE
FullInterface*
SOB_CLASS::create(TickPrice<TickRatio> min, TickPrice<TickRatio> max)
{
    if (min < 0 || min > max) {
        throw std::invalid_argument("min < 0 || min > max");
    }
    if (min == 0) {
        ++min; /* note: we adjust w/o client knowing */
    }

    // make inclusive
    size_t incr = static_cast<size_t>((max - min).as_ticks()) + 1;
    if (incr < 3) {
        throw std::invalid_argument("need at least 3 ticks");
    }

    FullInterface *tmp = new SimpleOrderbookImpl(min, incr);
    if (tmp) {
        if (!rmanager.add(tmp, master_rmanager)) {
            delete tmp;
            throw std::runtime_error("failed to add orderbook");
        }
    }
    return tmp;
}


SOB_TEMPLATE
void
SOB_CLASS::_grow_book(TickPrice<TickRatio> min, size_t incr, bool at_beg)
{
    if( incr == 0 ){
        return;
    }

    plevel old_beg = _beg;
    plevel old_end = _end;
#ifndef NDEBUG
    size_t old_sz = _book.size();
#endif

    std::lock_guard<std::mutex> lock(_master_mtx); 
    /* --- CRITICAL SECTION --- */
    
    std::vector<chain_pair_type> tmp( _book.size() + incr );    
    std::move( _book.begin(), _book.end(), tmp.begin() + (at_beg ? incr : 0) );
    _book = std::move(tmp);
    
    /* book is now in an INVALID state */

    _base = min;
    _beg = &(*_book.begin()) + 1;
    _end = &(*_book.end());

    long long offset = at_beg ? bytes_offset(_end, old_end)
                              : bytes_offset(_beg, old_beg);

    assert( equal(
        bytes_offset(_end, _beg),
        bytes_offset(old_end, old_beg)
            + static_cast<long long>(sizeof(*_beg) * incr),
        static_cast<long long>((old_sz + incr - 1) * sizeof(*_beg)),
        static_cast<long long>((_book.size() - 1) * sizeof(*_beg))
    ) );

    // even 0 offset needs to be handled 
    _reset_internal_pointers(old_beg, _beg, old_end, _end, offset);

    /* book is now in a VALID state */

    _assert_internal_pointers();
    /* --- CRITICAL SECTION --- */
}


SOB_TEMPLATE
typename SOB_CLASS::plevel
SOB_CLASS::_ptoi(TickPrice<TickRatio> price) const
{  /*
    * the range check asserts in here are 1 position more restrictive to catch
    * bad user price data passed but allow internal index conversions(_itop)
    *
    * this means that internally we should not convert to a price when
    * a pointer is past beg/at end, signaling a null value
    */
    long long offset = (price - _base).as_ticks();
    plevel p = _beg + offset;
    _assert_plevel(p);
    return p;
}


SOB_TEMPLATE
TickPrice<TickRatio>
SOB_CLASS::_itop(plevel p) const
{
    _assert_plevel(p); // internal range and align check
    return _base + plevel_offset(p, _beg);
}


};

#undef SOB_TEMPLATE
#undef SOB_CLASS
