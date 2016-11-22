/*
Copyright (C) 2015 Jonathon Ogden < jeog.dev@gmail.com >

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

#include "../simpleorderbook.hpp"
#include "common_py.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE

/* try to make the nested types somewhat readable */
#define NL_SO NativeLayer::SimpleOrderbook

/* type string + what() from native exception */
#define THROW_PY_EXCEPTION_FROM_NATIVE(e) \
do{ \
    std::string msg; \
    msg.append(typeid(e).name()).append(": ").append(e.what()); \
    PyErr_SetString(PyExc_Exception, msg.c_str()); \
    return NULL; \
}while(0) 

#define CALLDOWN_FOR_STATE_WITH_TRY_BLOCK(apicall,sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB* self){ \
        try{ \
            NL_SO::FullInterface* sob = (NL_SO::FullInterface*)self->_sob; \
            return apicall(sob->sobcall()); \
        }catch(std::exception& e){ \
            THROW_PY_EXCEPTION_FROM_NATIVE(e); \
        } \
    }

CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, bid_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, ask_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, last_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, ask_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, bid_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, total_ask_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, total_bid_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, total_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, last_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLongLong, volume )

#define CALLDOWN_TO_DUMP_WITH_TRY_BLOCK(sobcall) \
    static PyObject* SOB_ ## sobcall(pySOB* self){ \
        try{ \
            NL_SO::FullInterface* sob = (NL_SO::FullInterface*)self->_sob; \
            sob->sobcall(); \
        }catch(std::exception& e){ \
            THROW_PY_EXCEPTION_FROM_NATIVE(e); \
        } \
        Py_RETURN_NONE; \
    }

CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_buy_limits )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_sell_limits )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_buy_stops )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_sell_stops )

static char okws[][16] = { 
    "id", 
    "stop",
    "limit",
    "size",
    "callback" 
};

/* NOTE: frmt string WON'T match order of varargs because of the callback arg */
template<typename... Types>
bool 
get_order_args(PyObject* args,
               PyObject* kwds,
               const char* frmt,
               char** kwlist,
               PyObject** callback,
               Types*... varargs)
{
    int rval;
    rval = callback
         ? PyArg_ParseTupleAndKeywords(args, kwds, frmt, kwlist, varargs..., callback)
         : PyArg_ParseTupleAndKeywords(args, kwds, frmt, kwlist, varargs...);

    if(!rval){
        PyErr_SetString(PyExc_ValueError, "error parsing args");
        return false;
    }

    if(callback){
        rval = PyCallable_Check(*callback);
        if(!rval){
            PyErr_SetString(PyExc_TypeError,"callback must be callable");
            return false;
        }
    }

    return true;
}


template<bool BuyNotSell, bool Replace>
PyObject* 
SOB_trade_limit(pySOB* self, PyObject* args, PyObject* kwds)
{
    using namespace NativeLayer;

    price_type limit;
    long size;
    PyObject* callback;
    bool ares;

    id_type id = 0;
    callback = PyLong_FromLong(1); //dummy

    if(Replace){
        static char* kwlist[] = {okws[0],okws[2],okws[3],okws[4],NULL};
        /* arg order to interface :::  id, limit, size, callback */
        ares = get_order_args(args, kwds, "kflO:callback", kwlist, 
                              &callback, &id, &limit, &size);
    }else{
        static char* kwlist[] = {okws[2],okws[3],okws[4],NULL};
        /* arg order to interface :::  limit, size, callback */
        ares = get_order_args(args, kwds, "flO:callback", kwlist, 
                              &callback, &limit, &size);
    }

    if(!ares)
        return NULL;

    if(size <= 0){
        PyErr_SetString(PyExc_ValueError, "size must be > 0");
        return NULL;
    }

    try{
        SimpleOrderbook::FullInterface* sob = (SimpleOrderbook::FullInterface*)self->_sob;
        /*
         * be careful with copy contruction/ ref passing of ExecCallbackWrap object
         * we need to copy into the order_map, can pass by reference elsewhere
         */
        order_exec_cb_type cb = order_exec_cb_type(ExecCallbackWrap(callback));

        id = Replace ? sob->replace_with_limit_order(id, BuyNotSell, limit, size, cb)
                     : sob->insert_limit_order(BuyNotSell, limit, size, cb);
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return PyLong_FromUnsignedLong(id);
}



template< bool BuyNotSell, bool Replace >
PyObject* 
SOB_trade_market(pySOB* self, PyObject* args, PyObject* kwds)
{
    using namespace NativeLayer;

    long size;
    PyObject* callback;
    bool ares;

    id_type id = 0;
    callback = PyLong_FromLong(1); //dummy

    if(Replace){
        static char* kwlist[] = {okws[0],okws[3],okws[4],NULL};
        /* arg order to interface :::  id, size, callback */
        ares = get_order_args(args, kwds, "klO:callback", kwlist, 
                              &callback, &id, &size);
    }else{
        static char* kwlist[] = {okws[3],okws[4],NULL};
        /* arg order to interface :::  size, callback */
        ares = get_order_args(args, kwds, "lO:callback", kwlist, 
                              &callback, &size);
    }

    if(!ares)
        return NULL;

    if(size <= 0){
        PyErr_SetString(PyExc_ValueError, "size must be > 0");
        return NULL;
    }

    try{
        SimpleOrderbook::FullInterface* sob = (SimpleOrderbook::FullInterface*)self->_sob;
        /*
         * be careful with copy contruction/ ref passing of ExecCallbackWrap object
         * we need to copy into the order_map, can pass by reference elsewhere
         */
        order_exec_cb_type cb = order_exec_cb_type(ExecCallbackWrap(callback));

        id = Replace ? sob->replace_with_market_order(id, BuyNotSell, size, cb)
                     : sob->insert_market_order(BuyNotSell, size, cb);
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyNotSell, bool Replace>
PyObject* 
SOB_trade_stop(pySOB* self,PyObject* args,PyObject* kwds)
{
    using namespace NativeLayer;

    price_type stop;
    long size;
    PyObject* callback;
    bool ares;

    id_type id = 0;
    callback = PyLong_FromLong(1); //dummy

    if(Replace){
        static char* kwlist[] = {okws[0],okws[1],okws[3],okws[4],NULL};
        /* arg order to interface :::  id, stop, size, callback */
        ares = get_order_args(args, kwds, "kflO:callback", kwlist, 
                              &callback, &id, &stop, &size);
    }else{
        static char* kwlist[] = {okws[1],okws[3],okws[4],NULL};
        /* arg order to interface :::  stop, size, callback */
        ares = get_order_args(args, kwds, "flO:callback", kwlist, 
                              &callback, &stop, &size);
    }

    if(!ares)
        return NULL;

    if(size <= 0){
        PyErr_SetString(PyExc_ValueError, "size must be > 0");
        return NULL;
    }

    try{
        SimpleOrderbook::FullInterface* sob = (SimpleOrderbook::FullInterface*)self->_sob;
        /*
         * be careful with copy contruction/ ref passing of ExecCallbackWrap object
         * we need to copy into the order_map, can pass by reference elsewhere
         */
        order_exec_cb_type cb = order_exec_cb_type(ExecCallbackWrap(callback));

        id = Replace ? sob->replace_with_stop_order(id, BuyNotSell, stop, size, cb)
                     : sob->insert_stop_order(BuyNotSell, stop, size, cb);
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return PyLong_FromUnsignedLong(id);
}


template<bool BuyNotSell, bool Replace>
PyObject* 
SOB_trade_stop_limit(pySOB* self, PyObject* args, PyObject* kwds)
{
    using namespace NativeLayer;

    price_type stop;
    price_type limit;
    long size;
    PyObject* callback;
    bool ares;

    id_type id = 0;
    callback = PyLong_FromLong(1); //dummy

    if(Replace){
        static char* kwlist[] = {okws[0],okws[1],okws[2],okws[3],okws[4],NULL};
        /* arg order to interface :::  id, stop, limit, size, callback */
        ares = get_order_args(args, kwds, "kfflO:callback", kwlist, 
                              &callback, &id, &stop, &limit, &size);
    }else{
        static char* kwlist[] = {okws[1],okws[2],okws[3],okws[4],NULL};
        /* arg order to interface :::  stop, limit, size, callback */
        ares = get_order_args(args, kwds, "fflO:callback", kwlist, 
                              &callback, &stop, &limit, &size);
    }

    if(!ares)
        return NULL;

    if(size <= 0){
        PyErr_SetString(PyExc_ValueError, "size must be > 0");
        return NULL;
    }

    try{
        SimpleOrderbook::FullInterface* sob =
            (SimpleOrderbook::FullInterface*)self->_sob;
        /*
         * be careful with copy contruction/ ref passing of ExecCallbackWrap object
         * we need to copy into the order_map, can pass by reference elsewhere
         */
        order_exec_cb_type cb = order_exec_cb_type(ExecCallbackWrap(callback));

        id = Replace ? sob->replace_with_stop_order(id, BuyNotSell, stop, limit, size, cb)
                     : sob->insert_stop_order(BuyNotSell, stop, limit, size, cb);

    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }
    return PyLong_FromUnsignedLong(id);
}


PyObject* 
SOB_pull_order(pySOB* self, PyObject* args, PyObject* kwds)
{
    using namespace NativeLayer;

    bool rval;
    id_type id;

    static char* kwlist[] = {okws[0],NULL};

    if(!get_order_args(args, kwds, "k", kwlist, nullptr, &id))
        return NULL;

    try{
        rval = ((SimpleOrderbook::FullInterface*)self->_sob)->pull_order(id);
    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return PyBool_FromLong((unsigned long)rval);
}


static PyObject* 
SOB_time_and_sales(pySOB* self, PyObject* args)
{
    using namespace NativeLayer;

    SimpleOrderbook::FullInterface* sob;
    long arg;
    size_type num;
    PyObject *list, *tup;

    if(!PyArg_ParseTuple(args, "l", &arg)){
        PyErr_SetString(PyExc_ValueError, "error parsing args");
        return NULL;
    }

    try{
        sob = (SimpleOrderbook::FullInterface*)self->_sob;

        const SimpleOrderbook
                ::QueryInterface
                ::time_and_sales_type& vec = sob->time_and_sales();

        auto biter = vec.cbegin();
        auto eiter = vec.cend();

        num = arg <= 0 ? vec.size() : (size_type)arg;

        list = PyList_New(std::min(vec.size(),(size_type)num));

        for(size_type i = 0; i < num && biter != eiter; ++i, ++biter)
        {
            std::string s = SimpleOrderbook
                                ::FullInterface
                                ::timestamp_to_str(std::get<0>(*biter));

            tup = Py_BuildValue("(s,f,k)", s.c_str(), 
                                std::get<1>(*biter), std::get<2>(*biter));
            PyList_SET_ITEM(list, i, tup);
        }

    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    return list;
}


template<NativeLayer::side_of_market Side>
static PyObject* 
SOB_market_depth(pySOB* self, PyObject* args,PyObject* kwds)
{
    using namespace NativeLayer;

    SimpleOrderbook::FullInterface* sob;
    SimpleOrderbook::QueryInterface::market_depth_type md;
    long depth;
    size_t indx;
    bool size_error;
    PyObject *list, *tup;

    static char keyword[][8] = { "depth" };
    static char* kwlist[] = {keyword[0],NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "l", kwlist, &depth))
    {
        PyErr_SetString(PyExc_ValueError, "error parsing args");
        return NULL;
    }

    if(depth <= 0){
        PyErr_SetString(PyExc_ValueError, "depth must be > 0");
        return NULL;
    }

    try{
        sob = (SimpleOrderbook::FullInterface*)self->_sob;

        switch(Side){
            case(side_of_market::bid): 
                md = sob->bid_depth(depth);    
                break;
            case(side_of_market::ask): 
                md = sob->ask_depth(depth); 
                break;
            case(side_of_market::both): 
                md = sob->market_depth(depth); 
                break;
        }

        indx = md.size();

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

        if(size_error)
            throw NativeLayer::invalid_state("market_depth size too large");

        list = PyList_New(indx);

        for(auto& elem : md){
            tup = Py_BuildValue("(f,k)",elem.first,elem.second);
            PyList_SET_ITEM(list, --indx, tup);
        }

    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }
    return list;
}


/*static*/ PyObject* 
SOB_add_market_makers_local(pySOB* self, PyObject* args)
{
    using namespace NativeLayer;

    PyObject *py_mms;
    int good_obj;
    size_type mm_num;
    market_makers_type pmms;
    MarketMaker_Py* mm;
    pyMM* mm_c;
    SimpleOrderbook::FullInterface* sob;

    if(!PyArg_ParseTuple(args, "O", &py_mms)) /* DO WEE NEED TO INCREF ?? */
    {
        PyErr_SetString(PyExc_ValueError, "error parsing args");
        return NULL;
    }

    try{
        sob = (SimpleOrderbook::FullInterface*)self->_sob;
        mm_num = PySequence_Size(py_mms);
        while(mm_num--){
            try{                       
                mm_c = (pyMM*)PySequence_GetItem(py_mms, mm_num); /* new ref */
                if(!mm_c)
                    throw invalid_state("NULL item in sequence");

                good_obj = PyObject_IsInstance((PyObject*)mm_c, (PyObject*)&pyMM_type);
                if(!good_obj){
                    Py_DECREF((PyObject*)mm_c);
                    std::string msg("element in sequence not valid market maker: ");
                    msg.append(std::to_string(mm_num));
                    throw invalid_parameters(msg.c_str());                    
                }else if(good_obj < 0){
                    Py_DECREF((PyObject*)mm_c);
                    return NULL;
                }
   
                mm = (MarketMaker_Py*)(mm_c->_mm);
                Py_DECREF(mm_c);

                if(!mm)
                    throw invalid_state("NULL item MarketMaker_Py* in tuple");                
          
            }catch(invalid_state&){ 
                throw; 
            }catch(invalid_parameters&){ 
                throw; 
            }catch(...){                 
            } 
            /*
             * DEBUG
             *
             * for now let's do the dangerous thing and maintain a reference to
             * the object(instead of moving via _move_to_new()) so we can access
             * the MM from outside the SOB
             *
             * DEBUG
             */  
            if(!mm_c->_valid){
                std::cerr<< "market maker at index [" << mm_num 
                         << "] is not valid (have you already added it?) and will be ignored" 
                         << std::endl;
                continue;
            }
            mm_c->_valid = false;
            pmms.push_back(pMarketMaker(mm));            
        }

        if(pmms.size())
            sob->add_market_makers(std::move(pmms));        
        else
            throw std::runtime_error("no market makers to add");

    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }
    Py_RETURN_NONE;
}


static PyObject* 
SOB_add_market_makers_native(pySOB* self, PyObject* args)
{
    using namespace NativeLayer;

    size_type mm_ty, mm_num, mm_1, mm_2, mm_3;
    int mm_4;
    market_makers_type pmms;
    SimpleOrderbook::FullInterface* sob;

    mm_3 = 0; /* <-- so we can check the optional arg */
    mm_4 = 1;
    /*
     * args :
     *    1) MM_TYPE
     *    2) mm_num
     *    3) type dependent varargs
     */                                                                            

    /* low/sz, high/max, max/ */
    if(!PyArg_ParseTuple(args,"kkkk|ki", &mm_ty, &mm_num, &mm_1, &mm_2, &mm_3, &mm_4))
    {
        PyErr_SetString(PyExc_ValueError, "error parsing args");
        return NULL;
    }

    try{
        sob = (SimpleOrderbook::FullInterface*)self->_sob;
        /*
         * slightly cleaner if we were to use the ::Factory method but we
         * shouldn't assume it will be defined in a sub-class
         */
        while( mm_num-- ){ /* potential issues with the bad args */
            switch(mm_ty){
            case(MM_RANDOM):
            {
                if(mm_2 < mm_1 || mm_3 < mm_2 || mm_1 == 0)
                    throw std::invalid_argument("invalid args (type,num,low,high,max)");

                switch(mm_4){
                    case (int)MarketMaker_Random::dispersion::none: 
                        break;
                    case (int)MarketMaker_Random::dispersion::low: 
                        break;
                    case (int)MarketMaker_Random::dispersion::moderate: 
                        break;
                    case (int)MarketMaker_Random::dispersion::high: 
                        break;
                    case (int)MarketMaker_Random::dispersion::very_high: 
                        break;
                    default:
                        throw std::invalid_argument("mm_4 not valid dispersion enum");
                }
                pmms.push_back(
                    pMarketMaker(
                        new MarketMaker_Random(mm_1, mm_2, mm_3,
                                               (MarketMaker_Random::dispersion)mm_4)
                    )
                );
            }
            break;

            case(MM_SIMPLE1):
            {
                if(mm_2 < mm_1 || mm_1 == 0)
                    throw std::invalid_argument("invalid args (type,num,sz,max)");
                pmms.push_back(pMarketMaker(new MarketMaker_Simple1(mm_1,mm_2)));
            } 
            break;

            default:
                throw std::runtime_error("invalid market maker type");
            }
        }
        sob->add_market_makers(std::move(pmms));

    }catch(std::exception& e){
        THROW_PY_EXCEPTION_FROM_NATIVE(e);
    }

    Py_RETURN_NONE;
}


static PyMethodDef pySOB_methods[] = {
    /* GET STATE */
    {"bid_price",(PyCFunction)SOB_bid_price, METH_NOARGS, "() -> float"},

    {"ask_price",(PyCFunction)SOB_ask_price, METH_NOARGS, "() -> float"},

    {"last_price",(PyCFunction)SOB_last_price, METH_NOARGS, "() -> float"},

    {"bid_size",(PyCFunction)SOB_bid_size, METH_NOARGS, "() -> int"},

    {"ask_size",(PyCFunction)SOB_ask_size, METH_NOARGS, "() -> int"},

    {"total_bid_size",(PyCFunction)SOB_total_bid_size, METH_NOARGS, "() -> int"},

    {"total_ask_size",(PyCFunction)SOB_total_ask_size, METH_NOARGS, "() -> int"},

    {"total_size",(PyCFunction)SOB_total_size, METH_NOARGS, "() -> int"},

    {"last_size",(PyCFunction)SOB_last_size, METH_NOARGS, "() -> int"},

    {"volume",(PyCFunction)SOB_volume, METH_NOARGS, "() -> int"},

    {"bid_depth",(PyCFunction)SOB_market_depth<NativeLayer::side_of_market::bid>,
     METH_VARARGS | METH_KEYWORDS,
     "(int depth) -> list of 2-tuples [(float,int),(float,int),..]"},

    {"ask_depth",(PyCFunction)SOB_market_depth<NativeLayer::side_of_market::ask>,
     METH_VARARGS | METH_KEYWORDS,
     "(int depth) -> list of 2-tuples [(float,int),(float,int),..]"},

    {"market_depth",(PyCFunction)SOB_market_depth<NativeLayer::side_of_market::both>,
     METH_VARARGS | METH_KEYWORDS,
     "(int depth) -> list of 2-tuples [(float,int),(float,int),..]"},

    /* DUMP */
    {"dump_buy_limits",(PyCFunction)SOB_dump_buy_limits, METH_NOARGS,
     "dump (to stdout) all active limit buy orders; () -> void"},

    {"dump_sell_limits",(PyCFunction)SOB_dump_sell_limits, METH_NOARGS,
     "dump (to stdout) all active limit sell orders; () -> void"},

    {"dump_buy_stops",(PyCFunction)SOB_dump_buy_stops, METH_NOARGS,
     "dump (to stdout) all active buy stop orders; () -> void"},

    {"dump_sell_stops",(PyCFunction)SOB_dump_sell_stops, METH_NOARGS,
     "dump (to stdout) all active sell stop orders; () -> void"},

    /* INSERT */
    {"buy_limit",(PyCFunction)SOB_trade_limit<true,false>,
     METH_VARARGS | METH_KEYWORDS,
     "buy limit order; (limit, size, callback) -> order ID"},

    {"sell_limit",(PyCFunction)SOB_trade_limit<false,false>,
     METH_VARARGS | METH_KEYWORDS,
     "sell limit order; (limit, size, callback) -> order ID"},

    {"buy_market",(PyCFunction)SOB_trade_market<true,false>,
     METH_VARARGS | METH_KEYWORDS,
     "buy market order; (size, callback) -> order ID"},

    {"sell_market",(PyCFunction)SOB_trade_market<false,false>,
     METH_VARARGS | METH_KEYWORDS,
     "sell market order; (size, callback) -> order ID"},

    {"buy_stop",(PyCFunction)SOB_trade_stop<true,false>, 
     METH_VARARGS | METH_KEYWORDS,
     "buy stop order; (stop, size, callback) -> order ID"},

    {"sell_stop",(PyCFunction)SOB_trade_stop<false,false>, 
     METH_VARARGS | METH_KEYWORDS,
     "sell stop order; (stop, size, callback) -> order ID"},

    {"buy_stop_limit",(PyCFunction)SOB_trade_stop_limit<true,false>,
     METH_VARARGS | METH_KEYWORDS,
     "buy stop limit order; (stop, limit, size, callback) -> order ID"},

    {"sell_stop_limit",(PyCFunction)SOB_trade_stop_limit<false,false>,
     METH_VARARGS | METH_KEYWORDS,
     "sell stop limit order; (stop, limit, size, callback) -> order ID"},

    /* PULL */
    {"pull_order",(PyCFunction)SOB_pull_order, METH_VARARGS | METH_KEYWORDS,
     "remove order; (id) -> success/failure(boolean)"},

    /* REPLACE */
    {"replace_with_buy_limit",(PyCFunction)SOB_trade_limit<true,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new buy limit order; "
     "(id, limit, size, callback) -> new order ID"},

    {"replace_with_sell_limit",(PyCFunction)SOB_trade_limit<false,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new sell limit order; "
     "(id, limit, size, callback) -> new order ID"},

    {"replace_with_buy_market",(PyCFunction)SOB_trade_market<true,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new buy market order; "
     "(id, size, callback) -> new order ID"},

    {"replace_with_sell_market",(PyCFunction)SOB_trade_market<false,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new sell market order; "
     "(id, size, callback) -> new order ID"},

    {"replace_with_buy_stop",(PyCFunction)SOB_trade_stop<true,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new buy stop order; "
     "(id, stop, size, callback) -> new order ID"},

    {"replace_with_sell_stop",(PyCFunction)SOB_trade_stop<false,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new sell stop order; "
     "(id, stop, size, callback) -> new order ID"},

    {"replace_with_buy_stop_limit",(PyCFunction)SOB_trade_stop_limit<true,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new buy stop limit order; " 
     "(id, stop, limit, size, callback) -> new order ID"},

    {"replace_with_sell_stop_limit",(PyCFunction)SOB_trade_stop_limit<false,true>,
     METH_VARARGS | METH_KEYWORDS, 
     "replace old order with new sell stop limit order; "
     "(id, stop, limit, size, callback) -> new order ID"},

    /* MARKET MAKERS */
    {"add_native_market_makers",(PyCFunction)SOB_add_market_makers_native,
     METH_VARARGS ,
     "add market makers to orderbook; (MM_TYPE, num, varargs...) -> void\n"
     "MM_TYPE: use the MM_[...] constants to indicate type of market maker\n"
     "num: how many market makers of this type to create\n"
     "varargs: variable args depending on the type's (C++) constructor"},

    {"add_local_market_makers",(PyCFunction)SOB_add_market_makers_local,
     METH_VARARGS ,
     "add market makers to orderbook; ( (mms,) ) -> void\n"
     "(mms,): a tuple of local(python) market makers (MarketMaker type)"},

    /* TIME & SALES */
    {"time_and_sales",(PyCFunction)SOB_time_and_sales, METH_VARARGS,
     "(size) -> list of 3-tuples [(str,float,int),(str,float,int),..] "},

    {NULL}
};


typedef std::pair<std::string, NL_SO::FullInterface::cnstr_type> sob_type_entry;

#define MAKE_SOB(I,S,T) std::make_pair(I, \
    std::make_pair(S, &(NL_SO::New<NL_SO::FullInterface,NL_SO::T>)))

const std::map<int,sob_type_entry> SOB_TYPES = {
    /*
     * (arbitrary const, const name in python, SimpleOrderbook explicit inst.)
     */
    MAKE_SOB(SOB_QUARTER_TICK,"SOB_QUARTER_TICK",QuarterTick),
    MAKE_SOB(SOB_TENTH_TICK,"SOB_TENTH_TICK",TenthTick),
    MAKE_SOB(SOB_THIRTYSECONDTH_TICK,"SOB_THIRTYSECONDTH_TICK",ThirtySecondthTick),
    MAKE_SOB(SOB_HUNDREDTH_TICK,"SOB_HUNDREDTH_TICK",HundredthTick),
    MAKE_SOB(SOB_THOUSANDTH_TICK,"SOB_THOUSANDTH_TICK",ThousandthTick),
    MAKE_SOB(SOB_TENTHOUSANDTH_TICK,"SOB_TENTHOUSANDTH_TICK",TenThousandthTick)
};

const std::map<int,std::string> MM_TYPES = {
    /*
     * (arbitrary const, const name in python)
     * note: construction handle in switch because of varargs
     */
    std::make_pair(MM_RANDOM,"MM_RANDOM"),
    std::make_pair(MM_SIMPLE1,"MM_SIMPLE1")
};


static PyObject* 
SOB_New(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    using namespace NativeLayer;

    pySOB* self;
    price_type price,low,high;
    int sobty;
    SimpleOrderbook::FullInterface* sob;

    static char kws[][16] = {"sob_type","price", "low", "high"};
    static char* kwlist[] = {kws[0], kws[1], kws[2], kws[3], NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "ifff", kwlist, &sobty, &price, &low, &high))
    {
        PyErr_SetString(PyExc_ValueError, "error parsing args to __new__");
        return NULL;
    }

    self = (pySOB*)type->tp_alloc(type,0);
    self->_sob = nullptr;

    if(self != NULL){
        try{
            if(price < low || high < price)
                throw std::invalid_argument("invalid args (type,price,low,high)");

            if(low == 0)
                throw std::invalid_argument("low must be > 0");

            sob = SOB_TYPES.at(sobty).second(price,low,high);
            if(!sob)
                throw std::runtime_error("self->_sob was not constructed");
            else
                self->_sob = (PyObject*)sob;

        }catch(const std::invalid_argument & e){
            PyErr_SetString(PyExc_ValueError, e.what());
        }catch(const std::runtime_error & e){
            PyErr_SetString(PyExc_RuntimeError, e.what());
        }catch(const std::exception & e){
            PyErr_SetString(PyExc_Exception, e.what());
        }
        if(PyErr_Occurred()){
            Py_DECREF(self);
            return NULL;
        }
    }

    return (PyObject*)self;
}


static void 
SOB_Delete(pySOB* self)
{
    if(self->_sob)
        delete (NL_SO::FullInterface*)(self->_sob);

    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyTypeObject pySOB_type = {
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
    "SimpleOrderbook: matching engine/interface for (vanilla) financial-market order types\n\n"
    "  type  ::  int  :: type of orderbook (e.g SOB_QUARTER_TICK)\n"
    "  price :: float :: price to start trading at\n"
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
    0, //(initproc)SOB_Init,
    0,
    SOB_New,
};


static struct PyModuleDef pySOB_mod_def = {
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


PyMODINIT_FUNC 
PyInit_simpleorderbook(void)
{
    PyObject* mod;

    if(PyType_Ready(&pySOB_type) < 0 || PyType_Ready(&pyMM_type) < 0)
        return NULL;

    mod = PyModule_Create(&pySOB_mod_def);
    if(!mod)
        return NULL;

    Py_INCREF(&pySOB_type);
    PyModule_AddObject(mod, "SimpleOrderbook", (PyObject*)&pySOB_type);

    /* python/marketmaker_py.cpp */
    Py_INCREF(&pyMM_type);
    PyModule_AddObject(mod, "MarketMaker", (PyObject*)&pyMM_type);

    /* native market maker types */
    for(auto& p : MM_TYPES)
        PyObject_SetAttrString(mod, p.second.c_str(), Py_BuildValue("i",p.first));

    /* simple orderbook types */
    for(auto& p : SOB_TYPES)
        PyObject_SetAttrString(mod, p.second.first.c_str(), Py_BuildValue("i",p.first));

    return mod;
}

#endif /* IGNORE_TO_DEBUG_NATIVE */


