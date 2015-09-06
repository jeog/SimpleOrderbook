/*
Copyright (C) 2015 Jonathon Ogden     < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see http://www.gnu.org/licenses.
*/

#include <Python.h>
#include <structmember.h>
#include "../types.hpp"
#include "../simple_orderbook.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE

class CallbackWrapper{
  PyObject* _callback;

public:
  CallbackWrapper(PyObject* callback)
    :
      _callback(callback)
    {
      Py_XINCREF(callback);
    }
  CallbackWrapper(const CallbackWrapper& obj)
    :
      _callback(obj._callback)
    {
     Py_XINCREF(obj._callback);
    }
  ~CallbackWrapper()
    {
      Py_XDECREF(this->_callback);
    }
  void operator()(NativeLayer::callback_msg msg,
                  NativeLayer::id_type id,
                  NativeLayer::price_type price,
                  NativeLayer::size_type size) const
  {
    PyObject* args = Py_BuildValue("kkfk", (int)msg, id, price, size);
    PyObject_CallObject(this->_callback, args);
    Py_DECREF(args);
  }
};

/*
 * consts to be defined in python
 * indicate the type of orderbook and market makers to construct
 */
#define MM_RANDOM 1
#define MM_SIMPLE1 2

#define SOB_QUARTER_TICK 1
#define SOB_TENTH_TICK 2
#define SOB_THIRTYSECONDTH_TICK 3
#define SOB_HUNDREDTH_TICK 4
#define SOB_THOUSANDTH_TICK 5
#define SOB_TENTHOUSANDTH_TICK 6

/* try to make the nested types somewhat readable */
#define NL_SO NativeLayer::SimpleOrderbook

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

typedef struct {
  PyObject_HEAD
  PyObject* _sob;
} pySOB;

#define CALLDOWN_FOR_STATE_WITH_TRY_BLOCK(apicall,sobcall) \
  static PyObject* VOB_ ## sobcall(pySOB* self){ \
    try{ \
      NativeLayer::SimpleOrderbook::FullInterface* sob = \
        (NativeLayer::SimpleOrderbook::FullInterface*)self->_sob; \
      return apicall( sob->sobcall()); \
    }catch(std::exception& e){ \
      PyErr_SetString(PyExc_Exception, e.what()); \
      return NULL; \
    } \
  }

CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, bid_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, ask_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyFloat_FromDouble, last_price )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, ask_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, bid_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLong, last_size )
CALLDOWN_FOR_STATE_WITH_TRY_BLOCK( PyLong_FromUnsignedLongLong, volume )

#define CALLDOWN_TO_DUMP_WITH_TRY_BLOCK(sobcall) \
  static PyObject* VOB_ ## sobcall(pySOB* self){ \
    try{ \
      NativeLayer::SimpleOrderbook::FullInterface* sob = \
        (NativeLayer::SimpleOrderbook::FullInterface*)self->_sob; \
      sob->sobcall(); \
    }catch(std::exception& e){ \
      PyErr_SetString(PyExc_Exception, e.what()); \
      return NULL; \
    } \
    Py_RETURN_NONE; \
  }

CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_buy_limits )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_sell_limits )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_buy_stops )
CALLDOWN_TO_DUMP_WITH_TRY_BLOCK( dump_sell_stops )

#define UNPACK_TRADE_TEMPL_ENTER_CALLS_BY_ORDER_TYPE( order ) \
  static PyObject* \
  VOB_buy_ ## order(pySOB* self, PyObject* args, PyObject* kwds){ \
    return VOB_trade_ ## order ## _<true,false>(self,args,kwds); \
  } \
  static PyObject* \
  VOB_sell_ ## order(pySOB* self, PyObject* args, PyObject* kwds){ \
    return VOB_trade_ ## order ## _<false,false>(self,args,kwds); \
  } \
  static PyObject* \
  VOB_replace_with_buy_ ## order(pySOB* self, PyObject* args, PyObject* kwds){ \
    return VOB_trade_ ## order ## _<true,true>(self,args,kwds); \
  } \
  static PyObject* \
  VOB_replace_with_sell_ ## order(pySOB* self, PyObject* args, PyObject* kwds){ \
    return VOB_trade_ ## order ## _<false,true>(self,args,kwds); \
  }

static char okws[][16] = { "id", "stop","limit","size","callback" };

template<typename... Types>
bool get_order_args(PyObject* args,
                    PyObject* kwds,
                    const char* frmt,
                    char** kwlist,
                    PyObject** callback,
                    Types*... varargs )
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

/*
 * NOTE: the order of args passed into the python call (*args,  *kwds)
 *  is different than what we pass into get_order_args because of varargs...
 */
template<bool BuyNotSell, bool Replace>
PyObject* VOB_trade_limit_(pySOB* self, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  price_type limit;
  size_type size;
  PyObject* callback;
  bool ares;

  id_type id = 0;
  callback = PyLong_FromLong(1); //dummy

  if(Replace){
    static char* kwlist[] = {okws[0],okws[2],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "kfkO:callback", kwlist, &callback,
                          &id, &limit, &size);
  }else{
    static char* kwlist[] = {okws[2],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "fkO:callback", kwlist, &callback, &limit,
                          &size);
  }

  if(!ares)
    return NULL;

  try{
    SimpleOrderbook::FullInterface* sob =
      (SimpleOrderbook::FullInterface*)self->_sob;
    /*
     * be careful with copy contruction/ ref passing of CallbackWrapper object
     * we need to copy into the order_map, can pass by reference elsewhere
     */
    callback_type cb = callback_type(CallbackWrapper(callback));

    id = Replace ? sob->replace_with_limit_order(id, BuyNotSell, limit, size, cb)
                 : sob->insert_limit_order(BuyNotSell, limit, size, cb);

  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }

  return PyLong_FromUnsignedLong(id);
}
UNPACK_TRADE_TEMPL_ENTER_CALLS_BY_ORDER_TYPE( limit )

template< bool BuyNotSell, bool Replace >
PyObject* VOB_trade_market_(pySOB* self, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  size_type size;
  PyObject* callback;
  bool ares;

  id_type id = 0;
  callback = PyLong_FromLong(1); //dummy

  if(Replace){
    static char* kwlist[] = {okws[0],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "kkO:callback", kwlist, &callback,
                         &id, &size);
  }else{
    static char* kwlist[] = {okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "kO:callback", kwlist, &callback, &size);
  }

  if(!ares)
    return NULL;

  try{
    SimpleOrderbook::FullInterface* sob =
      (SimpleOrderbook::FullInterface*)self->_sob;
    /*
     * be careful with copy contruction/ ref passing of CallbackWrapper object
     * we need to copy into the order_map, can pass by reference elsewhere
     */
    callback_type cb = callback_type(CallbackWrapper(callback));

    id = Replace ? sob->replace_with_market_order(id, BuyNotSell, size, cb)
                 : sob->insert_market_order(BuyNotSell, size, cb);


  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }

  return PyLong_FromUnsignedLong(id);
}
UNPACK_TRADE_TEMPL_ENTER_CALLS_BY_ORDER_TYPE( market )

template<bool BuyNotSell, bool Replace>
PyObject* VOB_trade_stop_(pySOB* self,PyObject* args,PyObject* kwds)
{
  using namespace NativeLayer;

  price_type stop;
  size_type size;
  PyObject* callback;
  bool ares;

  id_type id = 0;
  callback = PyLong_FromLong(1); //dummy

  if(Replace){
    static char* kwlist[] = {okws[0],okws[1],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "kfkO:callback", kwlist, &callback,
                          &id, &stop, &size);
  }else{
    static char* kwlist[] = {okws[1],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "fkO:callback", kwlist, &callback, &stop,
                          &size);
  }

  if(!ares)
    return NULL;

  try{
    SimpleOrderbook::FullInterface* sob =
      (SimpleOrderbook::FullInterface*)self->_sob;
    /*
     * be careful with copy contruction/ ref passing of CallbackWrapper object
     * we need to copy into the order_map, can pass by reference elsewhere
     */
    callback_type cb = callback_type(CallbackWrapper(callback));

    id = Replace ? sob->replace_with_stop_order(id, BuyNotSell, stop, size, cb)
                 : sob->insert_stop_order(BuyNotSell, stop, size, cb);

  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }
  return PyLong_FromUnsignedLong(id);
}
UNPACK_TRADE_TEMPL_ENTER_CALLS_BY_ORDER_TYPE( stop )

template<bool BuyNotSell, bool Replace>
PyObject* VOB_trade_stop_limit_(pySOB* self, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  price_type stop;
  price_type limit;
  size_type size;
  PyObject* callback;
  bool ares;

  id_type id = 0;
  callback = PyLong_FromLong(1); //dummy

  if(Replace){
    static char* kwlist[] = {okws[0],okws[1],okws[2],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "kffkO:callback", kwlist, &callback,
                         &id, &stop, &limit, &size);
  }else{
    static char* kwlist[] = {okws[1],okws[2],okws[3],okws[4],NULL};
    ares = get_order_args(args, kwds, "ffkO:callback", kwlist, &callback, &stop,
                          &limit, &size);
  }

  if(!ares)
    return NULL;

  try{
    SimpleOrderbook::FullInterface* sob =
      (SimpleOrderbook::FullInterface*)self->_sob;
    /*
     * be careful with copy contruction/ ref passing of CallbackWrapper object
     * we need to copy into the order_map, can pass by reference elsewhere
     */
    callback_type cb = callback_type(CallbackWrapper(callback));

    id = Replace
      ? sob->replace_with_stop_order(id, BuyNotSell, stop, limit, size, cb)
      : sob->insert_stop_order(BuyNotSell, stop, limit, size, cb);

  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }
  return PyLong_FromUnsignedLong(id);
}
UNPACK_TRADE_TEMPL_ENTER_CALLS_BY_ORDER_TYPE( stop_limit )

PyObject* VOB_pull_order(pySOB* self, PyObject* args, PyObject* kwds)
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
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }

  return PyBool_FromLong((unsigned long)rval);
}

static PyObject* VOB_time_and_sales(pySOB* self, PyObject* args)
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

    const SimpleOrderbook::QueryInterface::time_and_sales_type& vec =
        sob->time_and_sales();

    auto biter = vec.cbegin();
    auto eiter = vec.cend();
    num = arg <= 0 ? vec.size() : (size_type)arg;

    list = PyList_New(std::min(vec.size(),(size_type)num));

    for(size_type i = 0; i < num && biter != eiter; ++i, ++biter)
    {
      std::string s =
        SimpleOrderbook::FullInterface::timestamp_to_str(std::get<0>(*biter));
      tup = Py_BuildValue("(s,f,k)", s.c_str(), std::get<1>(*biter),
                          std::get<2>(*biter));
      PyList_SET_ITEM(list, i, tup);
    }
  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }

  return list;
}

static PyObject* VOB_add_market_makers(pySOB* self, PyObject* args)
{
  using namespace NativeLayer;

  size_type mm_ty, mm_num, mm_1, mm_2, mm_3;
  market_makers_type* pmms;
  SimpleOrderbook::FullInterface* sob;

  mm_3 = 0; /* <-- so we can check the optional arg */
  /*
   * args :
   *  1) MM_TYPE
   *  2) mm_num
   *  3) type dependent varargs
   */                                               /* low/sz, high/max, max/ */
  if(!PyArg_ParseTuple(args, "kkkk|k", &mm_ty, &mm_num, &mm_1,  &mm_2,  &mm_3))
  {
    PyErr_SetString(PyExc_ValueError, "error parsing args");
    return NULL;
  }

  try{
    pmms = new market_makers_type;
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
          pmms->push_back(pMarketMaker(new MarketMaker_Random(mm_1,mm_2,mm_3)));
        }
        break;
      case(MM_SIMPLE1):
        {
          if(mm_2 < mm_1 || mm_1 == 0)
            throw std::invalid_argument("invalid args (type,num,sz,max)");
          pmms->push_back(pMarketMaker(new MarketMaker_Simple1(mm_1,mm_2)));
        }
        break;
      default:
        throw std::runtime_error("invalid market maker type");
      }
    }

   sob->add_market_makers(std::move(*pmms));

  }catch(std::exception& e){
    PyErr_SetString(PyExc_Exception, e.what());
    if(pmms)
      delete pmms;
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyMethodDef pySOB_methods[] =
{
  /* GET STATE */
  {"bid_price",(PyCFunction)VOB_bid_price, METH_NOARGS, "() -> float"},
  {"ask_price",(PyCFunction)VOB_ask_price, METH_NOARGS, "() -> float"},
  {"last_price",(PyCFunction)VOB_last_price, METH_NOARGS, "() -> float"},
  {"bid_size",(PyCFunction)VOB_bid_size, METH_NOARGS, "() -> int"},
  {"ask_size",(PyCFunction)VOB_ask_size, METH_NOARGS, "() -> int"},
  {"last_size",(PyCFunction)VOB_last_size, METH_NOARGS, "() -> int"},
  {"volume",(PyCFunction)VOB_volume, METH_NOARGS, "() -> int"},
  /* DUMP */
  {"dump_buy_limits",(PyCFunction)VOB_dump_buy_limits, METH_NOARGS,
      "dump (to stdout) all active limit buy orders; () -> void"},
  {"dump_sell_limits",(PyCFunction)VOB_dump_sell_limits, METH_NOARGS,
      "dump (to stdout) all active limit sell orders; () -> void"},
  {"dump_buy_stops",(PyCFunction)VOB_dump_buy_stops, METH_NOARGS,
      "dump (to stdout) all active buy stop orders; () -> void"},
  {"dump_sell_stops",(PyCFunction)VOB_dump_sell_stops, METH_NOARGS,
      "dump (to stdout) all active sell stop orders; () -> void"},
  /* INSERT */
  {"buy_limit",(PyCFunction)VOB_buy_limit, METH_VARARGS | METH_KEYWORDS,
    "buy limit order; (limit, size, callback) -> order ID"},
  {"sell_limit",(PyCFunction)VOB_sell_limit, METH_VARARGS | METH_KEYWORDS,
    "sell limit order; (limit, size, callback) -> order ID"},
  {"buy_market",(PyCFunction)VOB_buy_market, METH_VARARGS | METH_KEYWORDS,
    "buy market order; (size, callback) -> order ID"},
  {"sell_market",(PyCFunction)VOB_sell_market, METH_VARARGS | METH_KEYWORDS,
    "sell market order; (size, callback) -> order ID"},
  {"buy_stop",(PyCFunction)VOB_buy_stop, METH_VARARGS | METH_KEYWORDS,
    "buy stop order; (stop, size, callback) -> order ID"},
  {"sell_stop",(PyCFunction)VOB_sell_stop, METH_VARARGS | METH_KEYWORDS,
    "sell stop order; (stop, size, callback) -> order ID"},
  {"buy_stop_limit",(PyCFunction)VOB_buy_stop_limit,
    METH_VARARGS | METH_KEYWORDS,
    "buy stop limit order; (stop, limit, size, callback) -> order ID"},
  {"sell_stop_limit",(PyCFunction)VOB_sell_stop_limit,
    METH_VARARGS | METH_KEYWORDS,
    "sell stop limit order; (stop, limit, size, callback) -> order ID"},
  /* PULL */
  {"pull_order",(PyCFunction)VOB_pull_order, METH_VARARGS | METH_KEYWORDS,
    "remove order; (id) -> success/failure(boolean)"},
  /* REPLACE */
  {"replace_with_buy_limit",(PyCFunction)VOB_replace_with_buy_limit,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new buy limit order; "
                                  "(id, limit, size, callback) -> new order ID"},
  {"replace_with_sell_limit",(PyCFunction)VOB_replace_with_sell_limit,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new sell limit order; "
                                  "(id, limit, size, callback) -> new order ID"},
  {"replace_with_buy_market",(PyCFunction)VOB_replace_with_buy_market,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new buy market order; "
                                  "(id, size, callback) -> new order ID"},
  {"replace_with_sell_market",(PyCFunction)VOB_replace_with_sell_market,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new sell market order; "
                                  "(id, size, callback) -> new order ID"},
  {"replace_with_buy_stop",(PyCFunction)VOB_replace_with_buy_stop,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new buy stop order; "
                                  "(id, stop, size, callback) -> new order ID"},
  {"replace_with_sell_stop",(PyCFunction)VOB_replace_with_sell_stop,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new sell stop order; "
                                  "(id, stop, size, callback) -> new order ID"},
  {"replace_with_buy_stop_limit",(PyCFunction)VOB_replace_with_buy_stop_limit,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new buy stop limit "
                                  "order; (id, stop, limit, size, callback) "
                                  "-> new order ID"},
  {"replace_with_sell_stop_limit",(PyCFunction)VOB_replace_with_sell_stop_limit,
    METH_VARARGS | METH_KEYWORDS, "replace old order with new sell stop limit "
                                  "order; (id, stop, limit, size, callback) "
                                  "-> new order ID"},
  /* MARKET MAKERS */
  {"add_market_makers",(PyCFunction)VOB_add_market_makers,
    METH_VARARGS | METH_KEYWORDS,
    "add market makers to orderbook; (MM_TYPE, num, varargs...) -> void\n"
    "MM_TYPE: use the MM_[...] constants to indicate type of market maker\n"
    "num: how many market makers of this type to create\n"
    "varargs: variable args depending on the type's (C++) constructor"},
  /* TIME & SALES */
  {"time_and_sales",(PyCFunction)VOB_time_and_sales, METH_VARARGS,
    "(size) -> list of 3-tuples [(int,float,int),(int,float,int),..] "},
  {NULL}
};

static PyObject* VOB_New(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  pySOB* self;
  price_type price,low,high;
  int sobty;
  SimpleOrderbook::FullInterface* sob;

  static char kws[][16] = {"sob_type","price", "low", "high"};
  static char* kwlist[] = {kws[0], kws[1], kws[2], kws[3], NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "ifff", kwlist, &sobty, &price,
                                  &low, &high))
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

static void VOB_Delete(pySOB* self)
{
  delete (NativeLayer::SimpleOrderbook::FullInterface*)(self->_sob);
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject pySOB_type =
{
  PyVarObject_HEAD_INIT(NULL,0)
  "simpleorderbook.SimpleOrderbook",
  sizeof(pySOB),
  0,
  (destructor)VOB_Delete,
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
  "SimpleOrderbook (implemented in C++)",
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
  0, //(initproc)VOB_Init,
  0,
  VOB_New,
};

static struct PyModuleDef pySOB_mod_def =
{
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

PyMODINIT_FUNC PyInit_simpleorderbook(void)
{
  PyObject* mod;

  if(PyType_Ready(&pySOB_type) < 0)
    return NULL;

  mod = PyModule_Create(&pySOB_mod_def);
  if(!mod)
    return NULL;

  Py_INCREF(&pySOB_type);
  PyModule_AddObject(mod, "SimpleOrderbook", (PyObject*)&pySOB_type);

  /* market maker types */
  for( auto& p : MM_TYPES )
    PyObject_SetAttrString(mod, p.second.c_str(), Py_BuildValue("i",p.first));

  /* simple orderbook types */
  for( auto& p : SOB_TYPES )
    PyObject_SetAttrString(mod, p.second.first.c_str(),
                           Py_BuildValue("i",p.first));

  return mod;
}

#endif /* IGNORE_TO_DEBUG_NATIVE */
