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

#include "../../include/common.hpp"
#include "../../include/simpleorderbook.hpp"
#include "../include/common_py.hpp"
#include "../include/argparse_py.hpp"
#include "../include/callback_py.hpp"

#ifndef IGNORE_TO_DEBUG_NATIVE

// TODO  dump ALL limits/stops

namespace {

struct pySOBBundle{
    sob::FullInterface *interface;
    sob::SimpleOrderbook::FactoryProxy<> proxy;
    pySOBBundle( sob::FullInterface *interface,
                 sob::SimpleOrderbook::FactoryProxy<> proxy)
        :
            interface(interface),
            proxy(proxy)
        {
        }
};

typedef struct {
    PyObject_HEAD
    PyObject *sob_bndl;
} pySOB;

constexpr sob::FullInterface*
to_interface(const pySOB *sob)
{ return ((pySOBBundle*)(sob->sob_bndl))->interface; }

template<typename T>
constexpr std::pair<int, std::pair<std::string, sob::DefaultFactoryProxy>>
sob_type_make_entry(int index, std::string name)
{
    return std::make_pair(index,
               std::make_pair(name,
                   sob::SimpleOrderbook::BuildFactoryProxy<T>()) );
}

const std::map<int, std::pair<std::string, sob::DefaultFactoryProxy>>
SOB_TYPES = {
    sob_type_make_entry<sob::quarter_tick>(1, "SOB_QUARTER_TICK"),
    sob_type_make_entry<sob::tenth_tick>(2, "SOB_TENTH_TICK"),
    sob_type_make_entry<sob::thirty_secondth_tick>(3, "SOB_THIRTY_SECONDTH_TICK"),
    sob_type_make_entry<sob::hundredth_tick>(4, "SOB_HUNDREDTH_TICK"),
    sob_type_make_entry<sob::thousandth_tick>(5, "SOB_THOUSANDTH_TICK"),
    sob_type_make_entry<sob::ten_thousandth_tick>(6, "SOB_TEN_THOUSANDTH_TICK")
};

const std::map<int, std::string>
CALLBACK_MESSAGES = {
    {static_cast<int>(sob::callback_msg::cancel), "MSG_CANCEL"},
    {static_cast<int>(sob::callback_msg::fill), "MSG_FILL"},
    {static_cast<int>(sob::callback_msg::stop_to_limit), "MSG_STOP_TO_LIMIT"}
};

const std::map<int, std::string>
SIDES_OF_MARKET = {
    {static_cast<int>(sob::side_of_market::bid), "SIDE_BID"},
    {static_cast<int>(sob::side_of_market::ask), "SIDE_ASK"},
    {static_cast<int>(sob::side_of_market::both), "SIDE_BOTH"}
};

#define CALLDOWN_FOR_STATE(apicall, rtype, sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB *self){ \
        rtype ret; \
        Py_BEGIN_ALLOW_THREADS \
        try{ \
            sob::FullInterface *ob = to_interface(self); \
            ret = ob->sobcall(); \
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
            sob::FullInterface *ob = to_interface(self); \
            ob->sobcall(); \
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

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::limit, &id, &limit, &size, &cb) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = to_interface(self);
        id = ReplaceOrder
           ? ob->replace_with_limit_order(id, BuyOrder, limit, size, wrap_cb(cb))
           : ob->insert_limit_order(BuyOrder, limit, size, wrap_cb(cb));
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

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::market, &id, &size, &cb) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = to_interface(self);
        id = ReplaceOrder
           ? ob->replace_with_market_order(id, BuyOrder, size, wrap_cb(cb))
           : ob->insert_market_order(BuyOrder, size, wrap_cb(cb));
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

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::stop, &id, &stop, &size, &cb) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = to_interface(self);
        id = ReplaceOrder
           ? ob->replace_with_stop_order(id, BuyOrder, stop, size, wrap_cb(cb))
           : ob->insert_stop_order(BuyOrder, stop, size, wrap_cb(cb));
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

    if( !OrderMethodArgs<ReplaceOrder>::parse(args, kwds,
            sob::order_type::stop_limit, &id, &stop, &limit, &size, &cb) )
    {
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = to_interface(self);
        id = ReplaceOrder
           ? ob->replace_with_stop_order(id, BuyOrder, stop, limit, size, wrap_cb(cb))
           : ob->insert_stop_order(BuyOrder, stop, limit, size, wrap_cb(cb));
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
    bool rval;
    sob::id_type id;

    static char* kwlist[] = {MethodArgs::id,NULL};
    if( !MethodArgs::parse(args, kwds, "k", kwlist, &id) ){
        return false;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        rval = to_interface(self)->pull_order(id);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyBool_FromLong(static_cast<long>(rval));
}

PyObject*
timesales_to_list(const sob::QueryInterface::timesale_vector_type& vec, size_t n)
{
    PyObject *list;
    try{
        list = PyList_New(n);
        if( n == 0 ){
            return list;
        }
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
        sob::FullInterface *ob = to_interface(self);
        auto vec = ob->time_and_sales();
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
        PyObject *dict;
        try{
            dict = PyDict_New();
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
SOB_market_depth(pySOB *self, PyObject *args,PyObject *kwds)
{
    long depth;

    static char* kwlist[] = {MethodArgs::depth, NULL};
    if( !MethodArgs::parse(args, kwds, "l", kwlist, &depth) ){
        return NULL;
    }
    if(depth <= 0){
        PyErr_SetString(PyExc_ValueError, "depth must be > 0");
        return NULL;
    }

    PyObject *dict;
    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = to_interface(self);
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


struct MDef{
    template<typename F>
    static constexpr PyMethodDef
    NoArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_NOARGS, desc}; }

    template<typename F>
    static constexpr PyMethodDef
    KeyArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_VARARGS | METH_KEYWORDS, desc}; }

    template<typename F>
    static constexpr PyMethodDef
    VarArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_VARARGS, desc}; }
};


PyMethodDef pySOB_methods[] = {
    MDef::NoArgs("min_price", SOB_min_price, "() -> float"),
    MDef::NoArgs("max_price", SOB_max_price, "() -> float"),
    MDef::NoArgs("tick_size", SOB_tick_size, "() -> float"),
    MDef::NoArgs("bid_price", SOB_bid_price, "() -> float"),
    MDef::NoArgs("ask_price", SOB_ask_price, "() -> float"),
    MDef::NoArgs("last_price", SOB_last_price, "() -> float"),
    MDef::NoArgs("bid_size", SOB_bid_size, "() -> int"),
    MDef::NoArgs("ask_size", SOB_ask_size, "() -> int"),
    MDef::NoArgs("total_bid_size", SOB_total_bid_size, "() -> int"),
    MDef::NoArgs("total_ask_size", SOB_total_ask_size, "() -> int"),
    MDef::NoArgs("total_size", SOB_total_size, "() -> int"),
    MDef::NoArgs("last_size", SOB_last_size, "() -> int"),
    MDef::NoArgs("volume", SOB_volume, "() -> int"),

    MDef::NoArgs("dump_buy_limits", SOB_dump_buy_limits,
                 "print to stdout all active buy limit orders"),
    MDef::NoArgs("dump_sell_limits", SOB_dump_sell_limits,
                 "print to stdout all active sell limit orders"),
    MDef::NoArgs("dump_buy_stops", SOB_dump_buy_stops,
                 "print to stdout all active buy stop orders"),
    MDef::NoArgs("dump_sell_stops", SOB_dump_sell_stops,
                 "print to stdout all active sell stop orders"),


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
"    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
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
    "    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
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
    "    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
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
    "    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
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
    "    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
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
    "    callback :: (int,int,float,int)->(void) :: execution callback \n\n" \
    "    returns -> int \n"

    MDef::KeyArgs("replace_with_buy_stop_limit",SOB_trade_stop_limit<true,true>,
        DOCS_REPLACE_WITH_STOP_AND_LIMIT("buy")),

    MDef::KeyArgs("replace_with_sell_stop_limit",SOB_trade_stop_limit<false,true>,
        DOCS_REPLACE_WITH_STOP_AND_LIMIT("sell")),


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

PyObject*
SOB_New(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    pySOB *self;
    double low, high;
    int sobty;

    static char* kwlist[] = { MethodArgs::sob_type, MethodArgs::low,
                              MethodArgs::high, NULL };
    if( !MethodArgs::parse(args, kwds, "idd", kwlist, &sobty, &low, &high) ){
        return NULL;
    }
    if(low == 0){
        PyErr_SetString(PyExc_ValueError, "low == 0");
        return NULL;
    }
    if(low > high){ // not consistent with native checks
        PyErr_SetString(PyExc_ValueError, "low > high");
        return NULL;
    }

    if( SOB_TYPES.find(sobty) == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return NULL;
    }
    sob::DefaultFactoryProxy proxy(SOB_TYPES.at(sobty).second);

    self = (pySOB*)type->tp_alloc(type,0);
    if( !self ){
        PyErr_SetString(PyExc_MemoryError, "pySOB_type->tp_alloc failed");
        return NULL;
    }

    sob::FullInterface *ob = nullptr;
    pySOBBundle *bndl = nullptr;
    Py_BEGIN_ALLOW_THREADS
    try{
        sob::FullInterface *ob = proxy.create(low,high);
        bndl = new pySOBBundle(ob, proxy);
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
        if( bndl ){
            delete bndl;
        }
        if( ob ){
            Py_BEGIN_ALLOW_THREADS
            proxy.destroy(ob);
            Py_END_ALLOW_THREADS
        }
        Py_DECREF(self);
        return NULL;
    }

    self->sob_bndl = (PyObject*)bndl;
    return (PyObject*)self;
}


void
SOB_Delete(pySOB *self)
{
    if( self->sob_bndl ){
        pySOBBundle *bndl = ((pySOBBundle*)(self->sob_bndl));
        Py_BEGIN_ALLOW_THREADS
        bndl->proxy.destroy( bndl->interface );
        Py_END_ALLOW_THREADS
        self->sob_bndl = nullptr;
        delete bndl;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}


PyTypeObject pySOB_type = {
    PyVarObject_HEAD_INIT(NULL,0)
    "simpleorderbook.SimpleOrderbook",
    sizeof(pySOB),
    0,
    (destructor)SOB_Delete,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    "SimpleOrderbook: interface for a C++ financial orderbook and matching engine.\n\n"
    "  type  ::  int  :: type of orderbook (e.g SOB_QUARTER_TICK)\n"
    "  low   :: float :: minimum price can trade at\n"
    "  high  :: float :: maximum price can trade at\n" ,
    0,
    0,
    0,
    0,
    0,
    0,
    pySOB_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    SOB_New,
};


struct PyModuleDef module_def= {
    PyModuleDef_HEAD_INIT,
    "simpleorderbook",
    NULL,
    -1,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


PyObject*
atexit_callee(PyObject *self, PyObject *args)
{
    exiting_pre_finalize = true;
    Py_RETURN_NONE;
}

void
register_atexit_callee()
{
    static PyMethodDef def = {
            "__atexit_callee", atexit_callee, METH_NOARGS, ""
    };
    PyCFunctionObject *func = (PyCFunctionObject *)PyCFunction_New(&def, NULL);
    if( func == NULL ){
        std::cerr << "warn: failed to create atexit_callee function object"
                  << std::endl;
        return;
    }

    PyObject *mod = PyImport_ImportModule("atexit");
    if( !mod ){
        std::cerr << "warn: failed to import 'atexit'" << std::endl;
        Py_DECREF(func);
        return;
    }

    PyObject *ret = PyObject_CallMethod(mod, "register", "O", func);
    Py_DECREF(func);
    Py_DECREF(mod);
    if( ret ){
        Py_DECREF(ret);
    }else{
        std::cerr<< "warn: failed to register atexit_callee" << std::endl;
    }
}

template<typename T>
inline std::string
attr_str(T val)
{ return val; }

template<typename T>
inline std::string
attr_str(const std::pair<std::string, T>& val)
{ return val.first; }

template<typename T>
void
set_const_attributes(PyObject *mod, const std::map<int, T>& m)
{
    for( const auto& p : m ){
        PyObject *indx = Py_BuildValue("i",p.first);
        PyObject_SetAttrString(mod, attr_str(p.second).c_str(), indx);
    }
}


}; /* namespace */

volatile bool exiting_pre_finalize = false;

PyMODINIT_FUNC 
PyInit_simpleorderbook(void)
{
    if( PyType_Ready(&pySOB_type) < 0 ){
        return NULL;
    }

    PyObject *mod = PyModule_Create(&module_def);
    if( !mod ){
        return NULL;
    }

    Py_INCREF(&pySOB_type);
    PyModule_AddObject(mod, "SimpleOrderbook", (PyObject*)&pySOB_type);

    set_const_attributes(mod, SOB_TYPES);
    set_const_attributes(mod, CALLBACK_MESSAGES);
    set_const_attributes(mod, SIDES_OF_MARKET);
    register_atexit_callee();

    PyEval_InitThreads();
    return mod;
}

std::string
to_string(PyObject *arg)
{
    // TODO inspect the PyObject
    std::stringstream ss;
    ss<< std::hex << static_cast<void*>(arg);
    return ss.str();
}

#endif /* IGNORE_TO_DEBUG_NATIVE */


