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

#include "../common.hpp"
#include "../simpleorderbook.hpp"

#ifndef IGNORE_TO_DEBUG_NATIVE

// TODO market_depth default to max size
//      fix timesales copy vs ref

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

/* type string + what() from native exception */
#define THROW_PY_EXCEPTION_FROM_NATIVE(e) \
do{ \
    std::string msg; \
    msg.append(typeid(e).name()).append(": ").append(e.what()); \
    PyErr_SetString(PyExc_Exception, msg.c_str()); \
    return NULL; \
}while(0) 

#define CALLDOWN_FOR_STATE(apicall, rtype, sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB *self){ \
        rtype ret; \
        Py_BEGIN_ALLOW_THREADS \
        try{ \
            sob::FullInterface *ob = to_interface(self); \
            ret = ob->sobcall(); \
        }catch(std::exception& e){ \
            Py_BLOCK_THREADS \
            THROW_PY_EXCEPTION_FROM_NATIVE(e); \
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
CALLDOWN_FOR_STATE_DOUBLE( incr_size )
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
            THROW_PY_EXCEPTION_FROM_NATIVE(e); \
            Py_UNBLOCK_THREADS \
        } \
        Py_END_ALLOW_THREADS \
        Py_RETURN_NONE; \
    }

CALLDOWN_TO_DUMP( dump_buy_limits )
CALLDOWN_TO_DUMP( dump_sell_limits )
CALLDOWN_TO_DUMP( dump_buy_stops )
CALLDOWN_TO_DUMP( dump_sell_stops )

volatile bool exiting_pre_finalize = false;

class PyFuncWrap {
protected:
    PyObject *const cb;

    explicit PyFuncWrap(PyObject *callback)
        :
            cb(callback)
        {
            if( valid_interpreter_state() ){
                PyGILState_STATE gs = PyGILState_Ensure();
                Py_XINCREF(callback);
                PyGILState_Release(gs);
            }
        }

    PyFuncWrap(const PyFuncWrap& obj)
        :
            cb(obj.cb)
        {
            if( valid_interpreter_state() ){
                PyGILState_STATE gs = PyGILState_Ensure();
                Py_XINCREF(obj.cb);
                PyGILState_Release(gs);
            }
        }

    inline bool
    valid_interpreter_state() const
    { return !exiting_pre_finalize; }

public:
    virtual
    ~PyFuncWrap()
    {
        if( !cb ){
            return;
        }
        /*
         * PyGILState_Ensure() is getting called too late on exit().
         * _PyRuntime.gilstate.autoInterpreterState == 0 fails assert.
         * We register atexit_callee w/ atexit mod to set 'exiting_pre_finalize'
         * after Py_Finalize() but BEFORE _PyGILState_Fini() is called;
         */
        if( valid_interpreter_state() ){
            /*
             * race condition if main thread sets exiting_pre_finalize to true
             * between valid_interpeter_state() and PyGILState_Ensure()
             * (leave for now because it only happens on exit)
             */
            PyGILState_STATE gs = PyGILState_Ensure();
            Py_DECREF(cb);
            PyGILState_Release(gs);
        }
    }

    inline operator
    bool() const
    { return cb; }
};


class ExecCallbackWrap
        : public PyFuncWrap {
    ExecCallbackWrap& operator=(const ExecCallbackWrap&);
    ExecCallbackWrap& operator=(ExecCallbackWrap&&);

public:
    explicit ExecCallbackWrap(PyObject *callback)
        : PyFuncWrap(callback) {}

    ExecCallbackWrap(const ExecCallbackWrap& obj)
        : PyFuncWrap(obj) {}

    void
    operator()(sob::callback_msg msg,
               sob::id_type id,
               double price,
               size_t size) const
    {   /*
         * currently we simply no-op if wrapper is built on a null PyObject;
         * if, in the future, we allow option of no callback to the C++ interface
         * and ignore the cb from within the engine WE SHOULD RE-DO THIS
         */
        if( !(*this) ){
            return;
        }
        /* see notes in destructor about this check */
        if( !valid_interpreter_state() ){
            std::cerr<< "* callback(" << std::hex << reinterpret_cast<void*>(cb)
                     << ") not called; invalid interpreter state *"
                     << std::endl;
            return;
        }
        /* ignore race condition here for same reason as in destructor */
        PyGILState_STATE gs = PyGILState_Ensure();
        PyObject *args = Py_BuildValue("kkdk", (int)msg, id, price, size);
        PyObject* res = PyObject_CallObject(cb, args);
        if( PyErr_Occurred() ){
            std::cerr<< "* callback(" << std::hex
                     << reinterpret_cast<void*>(cb)
                     <<  ") error * " << std::endl;
            PyErr_Print();
        }
        Py_XDECREF(res);
        Py_XDECREF(args);
        PyGILState_Release(gs);
    }
};

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

std::string
to_string(PyObject *arg)
{
    // TODO inspect the PyObject
    std::stringstream ss;
    ss<< std::hex << static_cast<void*>(arg);
    return ss.str();
}

using std::to_string;

class MethodArgs {
    template<typename T, typename... TArgs>
    static std::string
    build_arg_values_str(T *arg1, TArgs... targs)
    { return to_string(*arg1) + " " + build_arg_values_str(targs...); }

    template<typename T>
    static std::string
    build_arg_values_str(T *arg1)
    { return to_string(*arg1); }

    static std::string
    build_keywords_str(char **kwds){
        std::string s;
        for(int i = 0; kwds[i] != NULL; ++i){
            s += kwds[i];
            s += " ";
        }
        if( !s.empty() ){
            s.pop_back();
        }
        return s;
    }

public:
    static char id[], stop[], limit[], size[], callback[], depth[],
                sob_type[], low[], high[];

    template<typename ...TArgs>
    static bool
    extract( PyObject *args,
             PyObject *kwds,
             const char* frmt,
             char **kwlist,
             TArgs... targs )
    {
        if( !PyArg_ParseTupleAndKeywords(args, kwds, frmt, kwlist, targs...) ){
            PyErr_SetString(PyExc_ValueError, "error parsing args");
            std::cerr<< "*** parse error ***" << std::endl;
            print(std::cerr, frmt, kwlist, targs...);
            std::cerr<< "*** parse error ***" << std::endl;
            return false;
        }
        return true;
    }

    template<typename ...TArgs>
    static bool
    extract(PyObject *args, const char* frmt, TArgs... targs)
    {
        if( !PyArg_ParseTuple(args, frmt, targs...) ){
            PyErr_SetString(PyExc_ValueError, "error parsing args");
            std::cerr<< "*** parse error ***" << std::endl;
            print(std::cerr, frmt, targs...);
            std::cerr<< "*** parse error ***" << std::endl;
            return false;
        }
        return true;
    }

    template<typename... TArgs>
    static void
    print(std::ostream& out, std::string frmt, char **kw, TArgs... targs)
    {
        out << "format: " << frmt << std::endl
            << "keywords: " << build_keywords_str(kw) << std::endl
            << "values: " << build_arg_values_str(targs...) << std::endl;
    }


    template<typename... TArgs>
    static void
    print(std::ostream& out, std::string frmt, TArgs... targs)
    {
        out << "* format: " << frmt << std::endl
            << "* values: " << build_arg_values_str(targs...) << std::endl;
    }
};

char MethodArgs::id[] = "id";
char MethodArgs::stop[] = "stop";
char MethodArgs::limit[] = "limit";
char MethodArgs::size[] = "size";
char MethodArgs::callback[] = "callback";
char MethodArgs::depth[] = "depth";
char MethodArgs::sob_type[] = "sob_type";
char MethodArgs::low[] = "low";
char MethodArgs::high[] = "high";

class OrderMethodArgsBase
        : protected MethodArgs {
protected:
    static const std::map<sob::order_type, std::array<char*,6>> keywords;
    static const std::map<sob::order_type, std::string> format_strs;
};

const std::map<sob::order_type, std::array<char*,6>>
OrderMethodArgsBase::keywords = {
    {sob::order_type::limit, {id, limit, size, callback}},
    {sob::order_type::market, {id, size, callback}},
    {sob::order_type::stop, {id, stop, size, callback}},
    {sob::order_type::stop_limit, {id, stop, limit, size, callback}}
};

const std::map<sob::order_type, std::string>
OrderMethodArgsBase::format_strs = {
    {sob::order_type::limit, "kdl|O"},
    {sob::order_type::market, "kl|O"},
    {sob::order_type::stop, "kdl|O"},
    {sob::order_type::stop_limit, "kddl|O"}
};

template<bool Replace>
class OrderMethodArgs
        : protected OrderMethodArgsBase {
    static_assert( std::is_same<sob::id_type, unsigned long>::value,
                   "id_type != unsigned long (change frmt strings)" );

protected:
    /* IF compiler gives 'error: no matching function for ... check_args()'
       check order of (id_arg + targs) in caller */
    template<typename T, typename T2, typename ...TArgs>
    static bool
    check_args(T arg1, T2 arg2, TArgs... args)
    { return check_args(arg2, args...); }

    static bool
    check_args(long *size, PyObject **cb)
    {
        if( *cb && !PyCallable_Check(*cb) ){
            PyErr_SetString(PyExc_TypeError,"callback must be callable");
            return false;
        }
        if( *size <= 0 ){
            PyErr_SetString(PyExc_ValueError, "size must be > 0");
            return false;
        }
        return true;
    }

    using MethodArgs::extract;

public:
    template<typename IdTy, typename ...TArgs>
    static bool
    extract( PyObject *args,
             PyObject *kwds,
             sob::order_type ot,
             IdTy id_arg,
             TArgs... targs)
    {
        const char *frmt = format_strs.at(ot).c_str();
        char **kptr = const_cast<char**>(keywords.at(ot).data());
        if( !extract(args, kwds, frmt, kptr, id_arg, targs...) ){
            return false;
        }
        if( !check_args(id_arg, targs...) ){
            std::cerr<< "*** parse error ***" << std::endl;
            print(std::cerr, frmt, kptr, targs...);
            std::cerr<< "*** parse error ***" << std::endl;
            return false;
        }
        return true;
    }
};

template<>
class OrderMethodArgs<false>
        : public OrderMethodArgs<true>{
    using OrderMethodArgs<true>::extract;
public:
    template<typename IdTy, typename ...TArgs>
    static bool
    extract( PyObject *args,
             PyObject *kwds,
             sob::order_type ot,
             IdTy id_arg,
             TArgs... targs )
    {
        const char *frmt = format_strs.at(ot).c_str() + 1;
        char **kptr = const_cast<char**>(keywords.at(ot).data()) + 1;
        if( !extract(args, kwds, frmt, kptr, targs...) ){
            return false;
        }
        if( !check_args(targs...) ){
            std::cerr<< "*** parse error ***" << std::endl;
            print(std::cerr, frmt, kptr, targs...);
            std::cerr<< "*** parse error ***" << std::endl;
            return false;
        }
        return true;
    }
};


template<bool BuyOrder, bool Replace, typename X=OrderMethodArgs<Replace>>
PyObject* 
SOB_trade_limit(pySOB *self, PyObject *args, PyObject *kwds)
{
    using namespace sob;
    double limit;
    long size;
    id_type id = 0;
    PyObject *py_cb = nullptr;

    if( !X::extract(args, kwds, order_type::limit, &id, &limit, &size, &py_cb) ){
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        order_exec_cb_type cb = ExecCallbackWrap(py_cb);
        id = Replace
           ? ob->replace_with_limit_order(id, BuyOrder, limit, size, cb)
           : ob->insert_limit_order(BuyOrder, limit, size, cb);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS // unnecessary, unless we change THROW...NATIVE()
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool Replace, typename X=OrderMethodArgs<Replace>>
PyObject* 
SOB_trade_market(pySOB *self, PyObject *args, PyObject *kwds)
{
    using namespace sob;
    long size;
    id_type id = 0;
    PyObject *py_cb = nullptr;

    if( !X::extract(args, kwds, order_type::market, &id, &size, &py_cb) ){
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        order_exec_cb_type cb = ExecCallbackWrap(py_cb);
        id = Replace
           ? ob->replace_with_market_order(id, BuyOrder, size, cb)
           : ob->insert_market_order(BuyOrder, size, cb);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool Replace, typename X=OrderMethodArgs<Replace>>
PyObject* 
SOB_trade_stop(pySOB *self,PyObject *args,PyObject *kwds)
{
    using namespace sob;
    double stop;
    long size;
    id_type id = 0;
    PyObject *py_cb = nullptr;

    if( !X::extract(args, kwds, order_type::stop, &id, &stop, &size, &py_cb) ){
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        order_exec_cb_type cb = ExecCallbackWrap(py_cb);
        id = Replace
           ? ob->replace_with_stop_order(id, BuyOrder, stop, size, cb)
           : ob->insert_stop_order(BuyOrder, stop, size, cb);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyOrder, bool Replace, typename X=OrderMethodArgs<Replace>>
PyObject* 
SOB_trade_stop_limit(pySOB *self, PyObject *args, PyObject *kwds)
{
    using namespace sob;
    double stop;
    double limit;
    long size;
    id_type id = 0;
    PyObject *py_cb = nullptr;

    if( !X::extract(args, kwds, order_type::stop_limit, &id, &stop, &limit,
                    &size, &py_cb) ){
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        order_exec_cb_type cb = ExecCallbackWrap(py_cb);
        id = Replace
           ? ob->replace_with_stop_order(id, BuyOrder, stop, limit, size, cb)
           : ob->insert_stop_order(BuyOrder, stop, limit, size, cb);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(id);
}


PyObject* 
SOB_pull_order(pySOB *self, PyObject *args, PyObject *kwds)
{
    using namespace sob;
    bool rval;
    id_type id;

    static char* kwlist[] = {MethodArgs::id,NULL};
    if( !MethodArgs::extract(args, kwds, "k", kwlist, &id) ){
        return false;
    }

    Py_BEGIN_ALLOW_THREADS
    try{
        rval = to_interface(self)->pull_order(id);
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    return PyBool_FromLong((unsigned long)rval);
}


PyObject*
SOB_time_and_sales(pySOB *self, PyObject *args)
{
    using namespace sob;

    long arg = -1;
    if( !MethodArgs::extract(args, "|l", &arg) ){
        return NULL;
    }

    QueryInterface::timesale_vector_type vec;
    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        vec = ob->time_and_sales();
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    PyObject *list;
    try{
        size_t num = arg <= 0 ? vec.size() : std::min(vec.size(),(size_t)arg);
        list = PyList_New(num);
        auto biter = vec.cbegin();
        for(size_t i = 0; i < num; ++i, ++biter)
        {
            std::string s = sob::to_string(std::get<0>(*biter));
            PyObject *tup = Py_BuildValue( "(s,d,k)", s.c_str(),
                                           std::get<1>(*biter),
                                           std::get<2>(*biter) );
            PyList_SET_ITEM(list, i, tup);
        }
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }
    return list;
}


template<sob::side_of_market Side>
PyObject*
SOB_market_depth(pySOB *self, PyObject *args,PyObject *kwds)
{
    using namespace sob;
    long depth;

    static char* kwlist[] = {MethodArgs::depth, NULL};
    if( !MethodArgs::extract(args, kwds, "l", kwlist, &depth) ){
        return NULL;
    }
    if(depth <= 0){
        PyErr_SetString(PyExc_ValueError, "depth must be > 0");
        return NULL;
    }

    std::map<double,size_t> md;
    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = to_interface(self);
        switch(Side){
            case(side_of_market::bid): 
                md = ob->bid_depth(depth);
                break;
            case(side_of_market::ask): 
                md = ob->ask_depth(depth);
                break;
            case(side_of_market::both): 
                md = ob->market_depth(depth);
                break;
        }
    }catch(std::exception& e){
        Py_BLOCK_THREADS
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
        Py_UNBLOCK_THREADS
    }
    Py_END_ALLOW_THREADS

    PyObject *list;
    try{
        size_t indx = md.size();
        bool size_error;
        switch(Side){
            case(side_of_market::bid): 
                /*no break*/
            case(side_of_market::ask): 
                size_error=(indx > (size_t)depth); 
                break;
            case(side_of_market::both): 
                size_error=(indx > ((size_t)depth * 2)); 
                break;
        }

        if(size_error){
            throw sob::invalid_state("market_depth size too large");
        }

        list = PyList_New(indx);
        for(auto& elem : md){
            PyObject *tup = Py_BuildValue("(f,k)",elem.first,elem.second);
            PyList_SET_ITEM(list, --indx, tup);
        }
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return list;
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
    MDef::NoArgs("max_price",SOB_max_price, "() -> float"),
    MDef::NoArgs("incr_size",SOB_incr_size, "() -> float"),
    MDef::NoArgs("bid_price",SOB_bid_price, "() -> float"),
    MDef::NoArgs("ask_price",SOB_ask_price, "() -> float"),
    MDef::NoArgs("last_price",SOB_last_price, "() -> float"),
    MDef::NoArgs("bid_size",SOB_bid_size, "() -> int"),
    MDef::NoArgs("ask_size",SOB_ask_size, "() -> int"),
    MDef::NoArgs("total_bid_size",SOB_total_bid_size, "() -> int"),
    MDef::NoArgs("total_ask_size",SOB_total_ask_size, "() -> int"),
    MDef::NoArgs("total_size",SOB_total_size, "() -> int"),
    MDef::NoArgs("last_size",SOB_last_size, "() -> int"),
    MDef::NoArgs("volume",SOB_volume, "() -> int"),

    MDef::NoArgs("dump_buy_limits",SOB_dump_buy_limits,
                 "print to stdout all active buy limit orders"),
    MDef::NoArgs("dump_sell_limits",SOB_dump_sell_limits,
                 "print to stdout all active sell limit orders"),
    MDef::NoArgs("dump_buy_stops",SOB_dump_buy_stops,
                 "print to stdout all active buy stop orders"),
    MDef::NoArgs("dump_sell_stops",SOB_dump_sell_stops,
                 "print to stdout all active sell stop orders"),


#define DOCS_MARKET_DEPTH(arg1) \
" get total outstanding order size at each " arg1 " price level \n\n" \
"    def " arg1 "_depth(depth) -> [(price level, total size), ...] \n\n" \
"    depth :: int :: number of price levels to return \n\n" \
"    returns -> list of (float,int) \n"

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
    using namespace sob;

    pySOB *self;
    double low, high;
    int sobty;

    static char* kwlist[] = { MethodArgs::sob_type, MethodArgs::low,
                              MethodArgs::high, NULL };
    if( !MethodArgs::extract(args, kwds, "idd", kwlist, &sobty, &low, &high) ){
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
    DefaultFactoryProxy proxy(SOB_TYPES.at(sobty).second);

    self = (pySOB*)type->tp_alloc(type,0);
    if( !self ){
        PyErr_SetString(PyExc_MemoryError, "pySOB_type->tp_alloc failed");
        return NULL;
    }

    FullInterface *ob = nullptr;
    pySOBBundle *bndl = nullptr;
    Py_BEGIN_ALLOW_THREADS
    try{
        FullInterface *ob = proxy.create(low,high);
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

}; /* namespace */


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

    /* simple orderbook types */
    for( auto& p : SOB_TYPES ){
        PyObject *indx = Py_BuildValue("i",p.first);
        PyObject_SetAttrString(mod, p.second.first.c_str(), indx);
    }

    /* callback_msg types */
    for( auto& p : CALLBACK_MESSAGES ){
        PyObject *indx = Py_BuildValue("i", p.first);
        PyObject_SetAttrString(mod, p.second.c_str(), indx);
    }

    register_atexit_callee();
    PyEval_InitThreads();
    return mod;
}

#endif /* IGNORE_TO_DEBUG_NATIVE */


