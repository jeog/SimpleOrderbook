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

#include <Python.h>
#include <structmember.h>
#include <sstream>
#include <iomanip>
#include <chrono>

#include "../../include/common.hpp"
#include "../../include/simpleorderbook.hpp"
#include "../include/common_py.hpp"
#include "../include/argparse_py.hpp"
#include "../include/callback_py.hpp"
#include "../include/advanced_order_py.hpp"

namespace {

typedef struct {
    PyObject_HEAD
    sob::FullInterface *interface;
    int sobty;
} pySOB;

#define CALLDOWN_FOR_STATE(apicall, rtype, sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB *self){ \
        rtype ret; \
        Py_BEGIN_ALLOW_THREADS \
        try{ \
            ret = self->interface->sobcall(); \
        }catch(std::exception& e){ \
            Py_BLOCK_THREADS \
            CONVERT_AND_THROW_NATIVE_EXCEPTION(e); \
            Py_UNBLOCK_THREADS \
        } \
        Py_END_ALLOW_THREADS \
        return apicall(ret); \
    }

#define CALLDOWN_FOR_STATE_DOUBLE(sobcall) \
        CALLDOWN_FOR_STATE(PyFloat_FromDouble, double, sobcall)

#define CALLDOWN_FOR_STATE_ULONG(sobcall) \
        CALLDOWN_FOR_STATE(PyLong_FromUnsignedLong, unsigned long, sobcall)

#define CALLDOWN_FOR_STATE_ULONGLONG(sobcall) \
        CALLDOWN_FOR_STATE(PyLong_FromUnsignedLongLong, unsigned long long, sobcall)

CALLDOWN_FOR_STATE_DOUBLE( min_price )
CALLDOWN_FOR_STATE_DOUBLE( max_price )
CALLDOWN_FOR_STATE_DOUBLE( tick_size )
CALLDOWN_FOR_STATE_DOUBLE( bid_price )
CALLDOWN_FOR_STATE_DOUBLE( ask_price )
CALLDOWN_FOR_STATE_DOUBLE( last_price )
CALLDOWN_FOR_STATE_ULONG( ask_size )
CALLDOWN_FOR_STATE_ULONG( bid_size )
CALLDOWN_FOR_STATE_ULONG( total_ask_size )
CALLDOWN_FOR_STATE_ULONG( total_bid_size )
CALLDOWN_FOR_STATE_ULONG( total_size )
CALLDOWN_FOR_STATE_ULONG( last_size )
CALLDOWN_FOR_STATE_ULONGLONG( volume )

#define CALLDOWN_TO_DUMP(sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB *self){ \
        Py_BEGIN_ALLOW_THREADS \
        try{ \
            self->interface->sobcall(); \
        }catch(std::exception& e){ \
            Py_BLOCK_THREADS \
            CONVERT_AND_THROW_NATIVE_EXCEPTION(e); \
            Py_UNBLOCK_THREADS \
        } \
        Py_END_ALLOW_THREADS \
        Py_RETURN_NONE; \
    }

CALLDOWN_TO_DUMP( dump_buy_limits )
CALLDOWN_TO_DUMP( dump_sell_limits )
CALLDOWN_TO_DUMP( dump_buy_stops )
CALLDOWN_TO_DUMP( dump_sell_stops )

#undef CALLDOWN_FOR_STATE
#undef CALLDOWN_FOR_STATE_DOUBLE
#undef CALLDOWN_FOR_STATE_ULONG
#undef CALLDOWN_FOR_STATE_ULONGLONG
#undef CALLDOWN_TO_DUMP


template<bool BuyOrder, bool ReplaceOrder>
PyObject*
SOB_trade_limit(pySOB *self, PyObject *args, PyObject *kwds)
{
    double limit;
    long size;
    sob::id_type id = 0;
    PyObject *cb = nullptr;
    PyObject *aot = nullptr;

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::limit, &id, &limit, &size, &cb, &aot) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::AdvancedOrderTicket native_aot = py_to_native_aot(aot);
        sob::FullInterface *ob = self->interface;
        id = ReplaceOrder
           ? ob->replace_with_limit_order(id, BuyOrder, limit, size,
                                          wrap_cb(cb), native_aot)
           : ob->insert_limit_order(BuyOrder, limit, size, wrap_cb(cb),
                                    native_aot);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS // unnecessary, unless we change THROW...NATIVE()
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool ReplaceOrder>
PyObject*
SOB_trade_market(pySOB *self, PyObject *args, PyObject *kwds)
{
    long size;
    sob::id_type id = 0;
    PyObject *cb = nullptr;
    PyObject *aot = nullptr;

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::market, &id, &size, &cb, &aot) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::AdvancedOrderTicket native_aot = py_to_native_aot(aot);
        sob::FullInterface *ob = self->interface;
        id = ReplaceOrder
           ? ob->replace_with_market_order(id, BuyOrder, size, wrap_cb(cb),
                                           native_aot)
           : ob->insert_market_order(BuyOrder, size, wrap_cb(cb),  native_aot);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool ReplaceOrder>
PyObject*
SOB_trade_stop(pySOB *self,PyObject *args,PyObject *kwds)
{
    double stop;
    long size;
    sob::id_type id = 0;
    PyObject *cb = nullptr;
    PyObject *aot = nullptr;

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::stop, &id, &stop, &size, &cb, &aot) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::AdvancedOrderTicket native_aot = py_to_native_aot(aot);
        sob::FullInterface *ob = self->interface;
        id = ReplaceOrder
           ? ob->replace_with_stop_order(id, BuyOrder, stop, size, wrap_cb(cb),
                                         native_aot)
           : ob->insert_stop_order(BuyOrder, stop, size, wrap_cb(cb), native_aot);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool ReplaceOrder>
PyObject*
SOB_trade_stop_limit(pySOB *self, PyObject *args, PyObject *kwds)
{
    double stop;
    double limit;
    long size;
    sob::id_type id = 0;
    PyObject *cb = nullptr;
    PyObject *aot = nullptr;

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::stop_limit, &id, &stop, &limit, &size, &cb, &aot) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::AdvancedOrderTicket native_aot = py_to_native_aot(aot);
        sob::FullInterface *ob = self->interface;
        id = ReplaceOrder
           ? ob->replace_with_stop_order(id, BuyOrder, stop, limit, size,
                                         wrap_cb(cb), native_aot)
           : ob->insert_stop_order(BuyOrder, stop, limit, size, wrap_cb(cb),
                                   native_aot);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


PyObject*
SOB_pull_order(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::id, NULL};

    sob::id_type id;
    if( !MethodArgs::parse(args, kwds, "k", kwlist, &id) ){
        return NULL;
    }

    bool rval = false;
    Py_BEGIN_ALLOW_THREADS
    try{
        rval = self->interface->pull_order(id);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyBool_FromLong(static_cast<long>(rval));
}


PyObject*
SOB_get_order_info(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::id, NULL};

    sob::id_type id;
    if( !MethodArgs::parse(args, kwds, "k", kwlist, &id) ){
        return NULL;
    }

    sob::order_info oi;
    Py_BEGIN_ALLOW_THREADS
    try{
        oi = self->interface->get_order_info(id);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    if( !oi ){
        Py_RETURN_NONE;
    }

    return pyOrderInfo_create(oi);
}


PyObject*
timesales_to_list(const std::vector<sob::timesale_entry_type>& vec, size_t n)
{
    PyObject *list = PyList_New(n);
    if( n == 0 ){
        return list;
    }
    try{
        /* reverse the order */
        auto eiter = vec.cend() - 1;
        for(size_t i = 0; i < n; ++i, --eiter){
            std::string s = sob::to_string(std::get<0>(*eiter));
            PyObject *tup = Py_BuildValue( "(s,d,k)", s.c_str(),
                                           std::get<1>(*eiter),
                                           std::get<2>(*eiter) );
            PyList_SET_ITEM(list, i, tup);
        }
    }catch(std::exception& e){
        Py_XDECREF(list);
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e); // sets Err, returns NULL
    }
    return list;
}


PyObject*
SOB_time_and_sales(pySOB *self, PyObject *args)
{
    long arg = -1;
    if( !MethodArgs::parse(args, "|l", &arg) ){
        return NULL;
    }

    PyObject *list;
    Py_BEGIN_ALLOW_THREADS
    try{
        auto vec = self->interface->time_and_sales();
        size_t vsz = vec.size();
        size_t n = (arg <= 0) ? vsz : std::min(vsz,(size_t)arg);
        Py_BLOCK_THREADS
        list = timesales_to_list(vec, n);
        Py_UNBLOCK_THREADS
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS
    return list;
}


template<sob::side_of_market Side = sob::side_of_market::both>
struct DepthHelper{
    template<typename T>
    static PyObject*
    to_dict(const std::map<double,T>& md)
    {
        PyObject *dict = PyDict_New();
        try{
            for(auto& level : md){
                auto p = build_pair(level.first, level.second);
                PyDict_SetItem(dict, std::get<0>(p), std::get<1>(p));
                Py_XDECREF(std::get<0>(p));
                Py_XDECREF(std::get<1>(p));
            }
        }catch(std::exception& e){
            Py_XDECREF(dict);
            CONVERT_AND_THROW_NATIVE_EXCEPTION(e); // set err, return NULL
        }
        return dict;
    }

    static inline std::map<double, std::pair<size_t, sob::side_of_market>>
    get(sob::FullInterface *ob, size_t depth)
    { return ob->market_depth(depth); }

private:
    inline static std::pair<PyObject*, PyObject*>
    build_pair(double d, size_t sz)
    { return std::make_pair( PyFloat_FromDouble(d), PyLong_FromSize_t(sz) ); }

    inline static std::pair<PyObject*, PyObject*>
    build_pair(double d, std::pair<size_t,sob::side_of_market> p)
    {
        return std::make_pair(
                PyFloat_FromDouble(d),
                Py_BuildValue("(K,i)", static_cast<unsigned long long>(p.first),
                              p.second)
        );
    }
};

template<>
struct DepthHelper<sob::side_of_market::bid>{
    static inline std::map<double, size_t>
    get(sob::FullInterface *ob, size_t depth)
    { return ob->bid_depth(depth); }
};

template<>
struct DepthHelper<sob::side_of_market::ask>{
    static inline std::map<double, size_t>
    get(sob::FullInterface *ob, size_t depth)
    { return ob->ask_depth(depth); }
};


template<sob::side_of_market Side>
PyObject*
SOB_market_depth(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::depth, NULL};

    long depth = 0;
    if( !MethodArgs::parse(args, kwds, "|l", kwlist, &depth) ){
        return NULL;
    }
    if(depth < 0){
        PyErr_SetString(PyExc_ValueError, "depth < 0");
        return NULL;
    }

    PyObject *dict;
    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = self->interface;
        if(depth == 0){
            depth = ob->ticks_in_range() + 1;
        }
        auto md = DepthHelper<Side>::get(ob, (size_t)depth);
        Py_BLOCK_THREADS
        dict = DepthHelper<>::to_dict(md);
        Py_UNBLOCK_THREADS
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return dict;
}


template<bool Above>
PyObject*
SOB_grow_book(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {
        Above ? Strings::new_max : Strings::new_min,
        NULL
    };

    double m;
    if( !MethodArgs::parse(args, kwds, "d", kwlist, &m) ){
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::ManagementInterface *ob =
            dynamic_cast<sob::ManagementInterface*>(self->interface);
        if( Above ){
            ob->grow_book_above(m);
        }else{
            ob->grow_book_below(m);
        }
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}

PyObject*
SOB_is_valid_price(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::price, NULL };

    double price;
    if( !MethodArgs::parse(args, kwds, "d", kwlist, &price) ){
        return NULL;
    }

    bool is_valid = false;
    try{
        is_valid = self->interface->is_valid_price(price);
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyBool_FromLong(static_cast<long>(is_valid));
}


PyObject*
SOB_price_to_tick(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::price, NULL};

    double price;

    if( !MethodArgs::parse(args, kwds, "d", kwlist, &price) ){
        return NULL;
    }

    double tick = 0;
    try{
        tick = self->interface->price_to_tick(price);
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyFloat_FromDouble(tick);
}


PyObject*
SOB_ticks_in_range(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::lower, Strings::upper, NULL};

    double lower = -1. * std::numeric_limits<double>::max();
    double upper = -1. * std::numeric_limits<double>::max();

    if( !MethodArgs::parse(args, kwds, "|dd", kwlist, &lower, &upper) ){
        return NULL;
    }

    long long ticks = 0;
    try{
        sob::FullInterface *ob = self->interface;
        // doesn't check for bad single arg
        if( lower == upper && upper == (-1. * std::numeric_limits<double>::max())){
            ticks = ob->ticks_in_range();
        }else if( !ob->is_valid_price(lower) || !ob->is_valid_price(upper) ){
            PyErr_SetString(PyExc_ValueError, "invalid price");
            return NULL;
        }else if( lower > upper ){
            PyErr_SetString(PyExc_ValueError, "lower > upper");
            return NULL;
        }else{
            ticks = ob->ticks_in_range(lower, upper);
        }
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyLong_FromLongLong(ticks);
}


PyObject*
SOB_tick_memory_required(pySOB *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::lower, Strings::upper, NULL };

    double lower = -1. * std::numeric_limits<double>::max();
    double upper = -1. * std::numeric_limits<double>::max();

    if( !MethodArgs::parse(args, kwds, "|dd", kwlist, &lower, &upper) ){
        return NULL;
    }

    unsigned long long mem = 0;
    try{
        sob::FullInterface *ob = self->interface;
        // doesn't check for bad single arg
        if( lower == upper && upper == (-1. * std::numeric_limits<double>::max())){
            mem = ob->tick_memory_required();
        }else if( !ob->is_valid_price(lower) || !ob->is_valid_price(upper) ){
            PyErr_SetString(PyExc_ValueError, "invalid price");
            return NULL;
        }else if( lower > upper ){
            PyErr_SetString(PyExc_ValueError, "lower > upper");
            return NULL;
        }else{
            mem = ob->tick_memory_required(lower, upper);
        }
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyLong_FromUnsignedLongLong(mem);
}


PyObject*
pySOB_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self = type->tp_alloc(type,0);
    if( !self ){
        PyErr_SetString(PyExc_MemoryError, "pySOB_type->tp_alloc failed");
    }
    return self;
}


int
pySOB_init(pySOB *self, PyObject *args, PyObject *kwds)
{
    double low, high;
    int sobty;

    static char* kwlist[] = { Strings::sob_type, Strings::low,
                              Strings::high, NULL };
    if( !MethodArgs::parse(args, kwds, "idd", kwlist, &sobty, &low, &high) ){
        return -1;
    }

    if(low == 0){
        PyErr_SetString(PyExc_ValueError, "low == 0");
        return -1;
    }
    if(low > high){ // not consistent with native checks
        PyErr_SetString(PyExc_ValueError, "low > high");
        return -1;
    }

    if( SOB_TYPES.find(sobty) == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return -1;
    }

    sob::DefaultFactoryProxy proxy(SOB_TYPES.at(sobty).second);
    sob::FullInterface *ob = nullptr;

    Py_BEGIN_ALLOW_THREADS
    try{
        ob = proxy.create(low,high);
    }catch(const std::runtime_error & e){
        Py_BLOCK_THREADS
        PyErr_SetString(PyExc_RuntimeError, e.what());
        Py_UNBLOCK_THREADS
    }catch(const std::exception & e){
        Py_BLOCK_THREADS
        PyErr_SetString(PyExc_Exception, e.what());
        Py_UNBLOCK_THREADS
    }catch(...){
        Py_BLOCK_THREADS
        Py_FatalError("fatal error creating orderbook");
        Py_UNBLOCK_THREADS // unnecessary
    }
    Py_END_ALLOW_THREADS

    if( PyErr_Occurred() ){
        if( ob ){
            Py_BEGIN_ALLOW_THREADS
            proxy.destroy(ob);
            Py_END_ALLOW_THREADS
        }
        Py_DECREF(self);
        return -1;
    }

    self->interface = ob;
    self->sobty = sobty;
    return 0;
}


void
pySOB_delete(pySOB *self)
{
    if( self->sobty && self->interface ){
        sob::DefaultFactoryProxy proxy(SOB_TYPES.at(self->sobty).second);
        Py_BEGIN_ALLOW_THREADS
        proxy.destroy( self->interface );
        Py_END_ALLOW_THREADS
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}


PyMethodDef pySOB_methods[] = {
    MDef::NoArgs("min_price", SOB_min_price, "minimum valid (tick) price"),
    MDef::NoArgs("max_price", SOB_max_price, "maximum valid (tick) price"),
    MDef::NoArgs("tick_size", SOB_tick_size, "size of individual tick"),
    MDef::NoArgs("bid_price", SOB_bid_price, "current bid price (0 if none)"),
    MDef::NoArgs("ask_price", SOB_ask_price, "current ask price (0 if none)"),
    MDef::NoArgs("last_price", SOB_last_price, "last price traded (0 if none)"),
    MDef::NoArgs("bid_size", SOB_bid_size, "size of current (inside) bid (0 if none)"),
    MDef::NoArgs("ask_size", SOB_ask_size, "size of current (inside) ask (0 if none)"),
    MDef::NoArgs("total_bid_size", SOB_total_bid_size, "size of all bids (0 if none)"),
    MDef::NoArgs("total_ask_size", SOB_total_ask_size, "size of all asks (0 if none)"),
    MDef::NoArgs("total_size", SOB_total_size, "size of all (limit) orders (0 if none)"),
    MDef::NoArgs("last_size", SOB_last_size, "last size traded (0 if none)"),
    MDef::NoArgs("volume", SOB_volume, "total volume traded"),

    MDef::NoArgs("dump_buy_limits", SOB_dump_buy_limits,
                 "print all active buy limit orders to stdout "),
    MDef::NoArgs("dump_sell_limits", SOB_dump_sell_limits,
                 "print all active sell limit orders to stdout "),
    MDef::NoArgs("dump_buy_stops", SOB_dump_buy_stops,
                 "print all active buy stop orders to stdout "),
    MDef::NoArgs("dump_sell_stops", SOB_dump_sell_stops,
                 "print all active sell stop orders to stdout "),

    MDef::KeyArgs("grow_book_above", SOB_grow_book<true>,
                  "increase the size of the orderbook from above \n\n"
                  "    def grow_book_above(new_max) -> None \n\n"
                  "    new_max :: float :: new maximum order/trade price"),

    MDef::KeyArgs("grow_book_below", SOB_grow_book<false>,
                  "increase the size of the orderbook from below \n\n"
                  "    def grow_book_below(new_min) -> None \n\n"
                  "    new_min :: float :: new minimum order/trade price"),

    MDef::KeyArgs("is_valid_price", SOB_is_valid_price,
                  "is price valid inside this book \n\n"
                  "    def is_valid_price(price) -> bool \n\n"
                  "    price :: float :: price to check"),

    MDef::KeyArgs("price_to_tick", SOB_price_to_tick,
                  "convert a price to a tick value \n\n"
                  "    def price_to_tick(price) -> tick \n\n"
                  "    price :: float :: price \n\n"
                  "    returns -> float \n"),

    MDef::KeyArgs("ticks_in_range", SOB_ticks_in_range,
                  "number of ticks between two prices \n\n"
                  "    def ticks_in_range(lower=min_price(), upper=max_price()) "
                  "-> number of ticks \n\n"
                  "    lower :: float :: lower price \n"
                  "    upper :: float :: upper price \n\n"
                  "    returns -> int \n"),

    MDef::KeyArgs("tick_memory_required", SOB_tick_memory_required,
                  "bytes of memory pre-allocated by orderbook internals. "
                  "THIS IS NOT TOTAL MEMORY BEING USED! \n\n"
                  "    def tick_memory_required(lower=min_price(), upper=max_price()) "
                  "-> number of bytes \n\n"
                  "    lower :: float :: lower price \n"
                  "    upper :: float :: upper price \n\n"
                  "    returns -> int \n"),

#define DOCS_MARKET_DEPTH(arg1) \
" get total outstanding order size at each " arg1 " price level \n\n" \
"    def " arg1 "_depth(depth) -> {price:size, price:size ...} \n\n" \
"    depth :: int :: number of price levels (per side) to return \n\n" \
"    returns -> dict of {float:int} \n"

    MDef::KeyArgs("bid_depth", SOB_market_depth<sob::side_of_market::bid>,
        DOCS_MARKET_DEPTH("bid")),

    MDef::KeyArgs("ask_depth",SOB_market_depth<sob::side_of_market::ask>,
        DOCS_MARKET_DEPTH("ask")),

    MDef::KeyArgs("market_depth",SOB_market_depth<sob::side_of_market::both>,
        DOCS_MARKET_DEPTH("market")),

#define DOCS_TRADE_MARKET(arg1) \
" insert " arg1 " market order \n\n" \
"    def " arg1 "_market(size, callback=None) -> order ID \n\n" \
"    size     :: int   :: number of shares/contracts \n" \
"    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
"    returns -> int \n"

    MDef::KeyArgs("buy_market",SOB_trade_market<true,false>,
        DOCS_TRADE_MARKET("buy")),

    MDef::KeyArgs("sell_market",SOB_trade_market<false,false>,
        DOCS_TRADE_MARKET("sell")),

#define DOCS_TRADE_STOP_OR_LIMIT(arg1, arg2) \
    " insert " arg1 " " arg2 " order \n\n" \
    "    def " arg1 "_" arg2 "(" arg2 ", size, callback=None) -> order ID \n\n" \
    "    " arg2 "     :: float :: " arg2 " price \n" \
    "    size     :: int   :: number of shares/contracts \n" \
    "    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("buy_limit",SOB_trade_limit<true,false>,
        DOCS_TRADE_STOP_OR_LIMIT("buy", "limit")),

    MDef::KeyArgs("sell_limit",SOB_trade_limit<false,false>,
        DOCS_TRADE_STOP_OR_LIMIT("sell", "limit")),

    MDef::KeyArgs("buy_stop",SOB_trade_stop<true,false>,
        DOCS_TRADE_STOP_OR_LIMIT("buy", "stop")),

    MDef::KeyArgs("sell_stop",SOB_trade_stop<false,false>,
        DOCS_TRADE_STOP_OR_LIMIT("sell", "stop")),

#define DOCS_TRADE_STOP_AND_LIMIT(arg1) \
    " insert " arg1 " stop-limit order \n\n" \
    "    def " arg1 "_stop_limit(stop, limit, size, callback=None)" \
    " -> order ID \n\n" \
    "    stop     :: float :: stop price \n" \
    "    limit    :: float :: limit price \n" \
    "    size     :: int   :: number of shares/contracts \n" \
    "    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("buy_stop_limit",SOB_trade_stop_limit<true,false>,
        DOCS_TRADE_STOP_AND_LIMIT("buy")),

    MDef::KeyArgs("sell_stop_limit",SOB_trade_stop_limit<false,false>,
        DOCS_TRADE_STOP_AND_LIMIT("sell")),

    MDef::KeyArgs("pull_order",SOB_pull_order,
        " pull(remove) order \n\n"
        "    def pull_order(id) -> success \n\n"
        "    id :: int :: order ID \n\n"
        "    returns -> bool \n"),

#define DOCS_REPLACE_WITH_MARKET(arg1) \
    " replace old order with new " arg1 " market order \n\n" \
    "    def replace_with_" arg1 "_market(id, size, callback=None)"\
    " -> new order ID \n\n" \
    "    id       :: int   :: old order ID \n" \
    "    size     :: int   :: number of shares/contracts \n" \
    "    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("replace_with_buy_market",SOB_trade_market<true,true>,
        DOCS_REPLACE_WITH_MARKET("buy")),

    MDef::KeyArgs("replace_with_sell_market",SOB_trade_market<false,true>,
        DOCS_REPLACE_WITH_MARKET("sell")),

#define DOCS_REPLACE_WITH_STOP_OR_LIMIT(arg1, arg2) \
    " replace old order with new " arg1 " " arg2 " order \n\n" \
    "    def replace_with_" arg1 "_" arg2 "(id, " arg2 ", size, callback=None)" \
    " -> new order ID \n\n" \
    "    id       :: int   :: old order ID \n" \
    "    " arg2 "    :: float :: " arg2 " price \n" \
    "    size     :: int   :: number of shares/contracts \n" \
    "    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("replace_with_buy_limit",SOB_trade_limit<true,true>,
        DOCS_REPLACE_WITH_STOP_OR_LIMIT("buy","limit")),

    MDef::KeyArgs("replace_with_sell_limit",SOB_trade_limit<false,true>,
            DOCS_REPLACE_WITH_STOP_OR_LIMIT("sell","limit")),

    MDef::KeyArgs("replace_with_buy_stop",SOB_trade_stop<true,true>,
            DOCS_REPLACE_WITH_STOP_OR_LIMIT("buy","stop")),

    MDef::KeyArgs("replace_with_sell_stop",SOB_trade_stop<false,true>,
            DOCS_REPLACE_WITH_STOP_OR_LIMIT("sell","stop")),

#define DOCS_REPLACE_WITH_STOP_AND_LIMIT(arg1) \
    " replace old order with new " arg1 " stop-limit order \n\n" \
    "    def replace_with_" arg1 "_stop_limit(id, stop, limit, size, callback=None)" \
    " -> new order ID \n\n" \
    "    id       :: int   :: old order ID \n" \
    "    stop     :: float :: stop price \n" \
    "    limit    :: float :: limit price \n" \
    "    size     :: int   :: number of shares/contracts \n" \
    "    callback :: (int,int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("replace_with_buy_stop_limit",SOB_trade_stop_limit<true,true>,
        DOCS_REPLACE_WITH_STOP_AND_LIMIT("buy")),

    MDef::KeyArgs("replace_with_sell_stop_limit",SOB_trade_stop_limit<false,true>,
        DOCS_REPLACE_WITH_STOP_AND_LIMIT("sell")),

    MDef::KeyArgs("get_order_info", SOB_get_order_info,
        " get order info for a particular ID \n\n"
        "    def get_order_info(id) -> OrderInfo \n\n"
        "    id :: int :: order ID \n\n"
        "    returns -> OrderInfo \n"),

    MDef::VarArgs("time_and_sales",SOB_time_and_sales,
        " get list of time & sales information \n\n"
        "    def time_and_sales(size) -> [(time,price,size),...] \n\n"
        "    size  ::  int  :: (optional) number of t&s tuples to return \n\n"
        "    returns -> list of (str,float,int)"),

    {NULL}
};

#undef DOCS_MARKET_DEPTH
#undef DOCS_TRADE_MARKET
#undef DOCS_TRADE_STOP_OR_LIMIT
#undef DOCS_TRADE_STOP_AND_LIMIT
#undef DOCS_REPLACE_WITH_MARKET
#undef DOCS_REPLACE_WITH_STOP_OR_LIMIT
#undef DOCS_REPLACE_WITH_STOP_AND_LIMIT

}; /* namespace */


PyTypeObject pySOB_type = {
    PyVarObject_HEAD_INIT(NULL,0)
    "simpleorderbook.SimpleOrderbook",
    sizeof(pySOB),
    0,
    (destructor)pySOB_delete,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    Py_TPFLAGS_DEFAULT,
    "SimpleOrderbook: interface for a C++ financial orderbook and matching engine.\n\n"
    "  type  ::  int  :: type of orderbook (e.g SOB_QUARTER_TICK)\n"
    "  low   :: float :: minimum price can trade at\n"
    "  high  :: float :: maximum price can trade at\n" ,
    0, 0, 0, 0, 0, 0,
    pySOB_methods,
    0, 0, 0, 0, 0, 0, 0,
    (initproc)pySOB_init,
    0,
    pySOB_new,
};

