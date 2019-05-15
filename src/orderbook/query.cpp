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

#include <iterator>
#include <iomanip>
#include <climits>

#include "../../include/simpleorderbook.hpp"
#include "../../include/order_util.hpp"
#include "specials.tpp"

#define SOB_CLASS SimpleOrderbook::SimpleOrderbookBase

// NOTE - only explicitly instantiate members needed for link and not
//        done implicitly. If (later) called from outside core.cpp
//        need to add them.

// TODO create helpers for shared functionality in aon query methods

namespace sob{


double
SOB_CLASS::bid_price() const
{
    using namespace detail;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    for( plevel h = _bid; h >= _low_buy_limit; --h ){
        if( chain<limit_chain_type>::atleast_if( h, 1, order::is_not_AON ) )
            return _itop(h);
    }
    return 0;
    /* --- CRITICAL SECTION --- */
}

double
SOB_CLASS::ask_price() const
{
    using namespace detail;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    for( plevel l = _ask; l <= _high_sell_limit; ++l ){
        if( chain<limit_chain_type>::atleast_if( l, 1, order::is_not_AON ) )
            return _itop(l);
    }
    return 0;
    /* --- CRITICAL SECTION --- */
}


double
SOB_CLASS::last_price() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return (_last >= _beg && _last < _end) ? _itop(_last) : 0.0;
    /* --- CRITICAL SECTION --- */
}


double
SOB_CLASS::min_price() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _itop(_beg);
    /* --- CRITICAL SECTION --- */
}


double
SOB_CLASS::max_price() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _itop(_end - 1);
    /* --- CRITICAL SECTION --- */
}


size_t
SOB_CLASS::bid_size() const
{
    using namespace detail;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    size_t tot = 0;
    for( plevel h = _bid; h >= _low_buy_limit && tot == 0; --h ){
        tot = chain<limit_chain_type>::size_if( h, order::is_not_AON );
    }
    return tot;
    /* --- CRITICAL SECTION --- */
}


size_t
SOB_CLASS::ask_size() const
{
    using namespace detail;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    size_t tot = 0;
    for( plevel l = _ask; l <= _high_sell_limit && tot == 0; ++l ){
        tot = chain<limit_chain_type>::size_if( l, order::is_not_AON );
    }
    return tot;
    /* --- CRITICAL SECTION --- */
}


size_t
SOB_CLASS::last_size() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _last_size;
    /* --- CRITICAL SECTION --- */
}


unsigned long long
SOB_CLASS::volume() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _total_volume;
    /* --- CRITICAL SECTION --- */
}


id_type
SOB_CLASS::last_id() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _last_id;
    /* --- CRITICAL SECTION --- */
}


const std::vector<timesale_entry_type>&
SOB_CLASS::time_and_sales() const
{
    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    return _timesales;
    /* --- CRITICAL SECTION --- */
}


void
SOB_CLASS::dump_internal_pointers(std::ostream& out) const
{
    double tick;

    auto println = [&](std::string n, plevel p){
        std::string price("N/A");
        try{
            if( p ){
                _assert_plevel(p);
                double d;
                if( p == &(*_book.end()) )
                    d = _itop(p-1) + tick;
                else if( p == &(*_book.begin()) )
                    d = _itop(p+1) - tick;
                else
                    d = _itop(p);
                price = std::to_string(d);
            }
        }catch(std::range_error&){}

        out<< std::setw(18) << n << " : "
           << std::setw(14) << price << " : "
           << std::hex << p << std::dec << std::endl;
    };


    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    // hack to get tick size - guaranteed >= 3 ticks
    tick = _itop(_beg+1) - _itop(_beg);

    std::ios sstate(nullptr);
    sstate.copyfmt(out);
    out<< "*** CACHED PLEVELS ***" << std::left << std::endl;
    println("_end", _end);
    println("_high_sell_limit", _high_sell_limit);
    println("_high_buy_stop", _high_buy_stop);
    println("_low_buy_stop", _low_buy_stop);
    println("_ask", _ask);
    println("_last", _last);
    println("_bid", _bid);
    println("_high_sell_stop", _high_sell_stop);
    println("_low_sell_stop", _low_sell_stop);
    println("_low_buy_limit", _low_buy_limit);
    println("_high_sell_aon", _high_sell_aon);
    println("_low_sell_aon", _low_sell_aon);
    println("_high_buy_aon", _high_buy_aon);
    println("_low_buy_aon", _low_buy_aon);
    println("_beg", _beg);
    out.copyfmt(sstate);
    /* --- CRITICAL SECTION --- */
}


/* orderbook depth of non-AON limit orders */
template<side_of_market Side>
std::map<double, typename std::conditional<Side == side_of_market::both,
                 std::pair<size_t, side_of_market>, size_t>::type >
SOB_CLASS::_limit_depth(size_t depth) const
{
    using namespace detail;
    using DEPTH = detail::depth<Side>;
    using RANGE = range<DEPTH::SIDE_OF_TRADE>;

    plevel h, l;
    std::map<double, typename DEPTH::mapped_type> md;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::tie(l,h) = RANGE::template get<limit_chain_type>(this,depth);
    for( ; h >= l; --h){
        if( !h->limit_chain_is_empty() ){
            size_t sz = chain<limit_chain_type>::size_if(h, order::is_not_AON);
            md.emplace( _itop(h), DEPTH::build_value(this, h, sz) );
        }
    }
    return md;
    /* --- CRITICAL SECTION --- */
}
template std::map<double,std::pair<size_t, side_of_market>>
SOB_CLASS::_limit_depth<side_of_market::both>(size_t) const;

template std::map<double,size_t>
SOB_CLASS::_limit_depth<side_of_market::bid>(size_t) const;

template std::map<double,size_t>
SOB_CLASS::_limit_depth<side_of_market::ask>(size_t) const;


std::map<double, std::pair<size_t,size_t>>
SOB_CLASS::aon_market_depth() const
{
    using namespace detail;
    using LC = chain<limit_chain_type>;
    using AC = chain<aon_chain_type>;

    auto pred = [](const limit_bndl& b){ return order::is_AON(b); };
    std::map<double, std::pair<size_t, size_t>> md;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    plevel l, h;
    std::tie(l,h) = range<>::template get<limit_chain_type, aon_chain_type>(this);
    for( ; l <= h; ++l ){
        size_t buy_sz = 0, sell_sz = 0;

        if( exec::limit<true>::is_tradable(this,l) )
            buy_sz += LC::size_if(l, pred );
        else if( exec::limit<false>::is_tradable(this,l) )
            sell_sz += LC::size_if(l, pred );

        buy_sz += AC::size<true>(l);
        sell_sz += AC::size<false>(l);

        if( buy_sz || sell_sz )
            md[_itop(l)] = {buy_sz, sell_sz};
    }
    return md;
    /* --- CRITICAL SECTION --- */
}


/* total size of limit/stop/aon buys/sells/both*/
template<side_of_trade Side, typename ChainTy>
size_t
SOB_CLASS::_total_depth() const
{
    using namespace detail;

    plevel h, l;
    size_t tot = 0;

    constexpr bool AON = chain<ChainTy>::is_aon;
    using FirstChain =
        typename std::conditional<AON, limit_chain_type, ChainTy>::type;

    auto pred = AON  ? [](const limit_bndl& b){ return order::is_AON(b); }
                     : [](const limit_bndl& b){ return order::is_not_AON(b); };

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */
    std::tie(l,h) = range<Side>::template get<FirstChain>(this);
    for( ; h >= l; --h){
        tot += chain<FirstChain>::size_if(h, pred );
    }

    if( AON ){
        std::tie(l,h) = range<Side>::template get<aon_chain_type>(this);
        for( ; h >= l; --h)
            tot += chain<aon_chain_type>::size<Side>( h );
    }

    return tot;
  /* --- CRITICAL SECTION --- */
}
template size_t
SOB_CLASS::_total_depth<side_of_trade::both, SOB_CLASS::limit_chain_type>() const;
template size_t
SOB_CLASS::_total_depth<side_of_trade::buy, SOB_CLASS::limit_chain_type>() const;
template size_t
SOB_CLASS::_total_depth<side_of_trade::sell, SOB_CLASS::limit_chain_type>() const;
template size_t
SOB_CLASS::_total_depth<side_of_trade::both, SOB_CLASS::aon_chain_type>() const;
template size_t
SOB_CLASS::_total_depth<side_of_trade::buy, SOB_CLASS::aon_chain_type>() const;
template size_t
SOB_CLASS::_total_depth<side_of_trade::sell, SOB_CLASS::aon_chain_type>() const;


/* all non-AON orders to 'out' */
template<side_of_trade Side, typename ChainTy>
void
SOB_CLASS::_dump_orders(std::ostream& out) const
{
    using namespace detail;
    static_assert( !chain<ChainTy>::is_aon, "use _aon_dump_orders");

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    out << "*** (" << Side << ") " << chain<ChainTy>::as_order_type()
        << "s ***" << std::endl;

    plevel l, h;
    std::tie(l,h) = range<Side>::template get<ChainTy>(this);
    for( ; h >= l; --h){
        auto c = chain<ChainTy>::get(h);
        if( !c->empty() ){
            std::stringstream ss;
            for( const auto& e : *c ){
                if ( order::is_not_AON(e) )
                    order::dump(ss, e, _is_buy_order(h, e));
            }
            if( !ss.str().empty() )
                out << _itop(h) << ss.str() << std::endl;
        }
    }
    /* --- CRITICAL SECTION --- */
}
template void SOB_CLASS::_dump_orders
<side_of_trade::both, SOB_CLASS::limit_chain_type>(std::ostream&) const;
template void SOB_CLASS::_dump_orders
<side_of_trade::buy, SOB_CLASS::limit_chain_type>(std::ostream&) const;
template void SOB_CLASS::_dump_orders
<side_of_trade::sell, SOB_CLASS::limit_chain_type>(std::ostream&) const;
template void SOB_CLASS::_dump_orders
<side_of_trade::both, SOB_CLASS::stop_chain_type>(std::ostream&) const;
template void SOB_CLASS::_dump_orders
<side_of_trade::buy, SOB_CLASS::stop_chain_type>(std::ostream&) const;
template void SOB_CLASS::_dump_orders
<side_of_trade::sell, SOB_CLASS::stop_chain_type>(std::ostream&) const;


template<side_of_trade Side>
void
SOB_CLASS::_dump_aon_orders(std::ostream& out) const
{
    using namespace detail;

    out << "*** (AON " << Side << " limits) ***" << std::endl;

    std::lock_guard<std::mutex> lock(_master_mtx);
    /* --- CRITICAL SECTION --- */

    plevel l, h;
    std::tie(l,h) = range<Side>::template
        get<limit_chain_type, aon_chain_type>(this);

    for( ; h >= l; --h ){
        std::stringstream ss;

        // bid/buy side first
        if( Side != side_of_trade::sell ){
            // aon chains first
            auto c = h->get_aon_chain<true>();
            if( c ){
                for( const auto& elem : *c )
                    order::dump(ss, elem, true);
            }
            if( exec::limit<true>::is_tradable(this,h) ){
                limit_chain_type *lc = h->get_limit_chain();
                for( auto& elem : *lc ){
                    if( order::is_AON(elem) )
                        order::dump(ss, elem, true);
                }
            }
        }

        if( Side != side_of_trade::buy ){
            // aon chains first
            auto c = h->get_aon_chain<false>();
            if( c ){
                for( const auto& elem : *c )
                    order::dump(ss, elem, false);
            }
            if( exec::limit<false>::is_tradable(this,h) ){
                limit_chain_type *lc = h->get_limit_chain();
                for( auto& elem : *lc ){
                    if( order::is_AON(elem) )
                        order::dump(ss, elem, false);
                }
            }
        }

        if( !ss.str().empty() )
            out << _itop(h) << ss.str() << std::endl;
    }
    /* --- CRITICAL SECTION --- */

}
template void
SOB_CLASS::_dump_aon_orders <side_of_trade::both>(std::ostream&) const;
template void
SOB_CLASS::_dump_aon_orders<side_of_trade::buy>(std::ostream&) const;
template void
SOB_CLASS::_dump_aon_orders<side_of_trade::sell>(std::ostream&) const;


} /* sob */
