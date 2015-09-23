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

#include "../market_maker_native/market_maker.hpp"
#include "common.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE



static char keywords[][20] = {"limit", "size", "no_order_cb",
                              "exec_callback_func", "start_func", "stop_func"};

template<bool BuyNotSell>
PyObject* MM_insert(pyMM* self, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;
  price_type limit;
  size_type size;
  bool no_order_cb = false;

  static char* kwlist[] = {keywords[0],keywords[1],keywords[2],NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "fk|b", kwlist, &limit, &size,
                                  &no_order_cb))
  {
    PyErr_SetString(PyExc_ValueError, "error parsing args");
    return NULL;
  }

  try{
    ((MarketMaker_Py*)(self->_mm))->insert<BuyNotSell>(limit,size,no_order_cb);
  }
  catch(std::exception& e)
  {
    PyErr_SetString(PyExc_Exception, e.what());
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyMethodDef pyMM_methods[] =
{
  {"insert_buy",(PyCFunction)MM_insert<true>, METH_VARARGS | METH_KEYWORDS,
    "insert buy order (float,int,bool(optional)) -> void"},
  {"insert_sell",(PyCFunction)MM_insert<false>, METH_VARARGS | METH_KEYWORDS,
    "insert sell order (float,int,bool(optional)) -> void"},
  {NULL}
};


static PyObject* MM_New(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  pyMM* self;
  MarketMaker_Py* mm;
  PyObject *start, *stop, *cb;

  start = nullptr;
  stop = nullptr;
  cb = nullptr;

  static char* kwlist[] = {keywords[3],keywords[4],keywords[5],NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "O|OO:__new__", kwlist,
                                  &cb, &start, &stop))
    PyErr_SetString(PyExc_ValueError, "error parsing args to __new__");
  else if(!PyCallable_Check(cb))
    PyErr_SetString(PyExc_TypeError, "exec_callback_func must be callable");
  else if(start && !PyCallable_Check(start))
    PyErr_SetString(PyExc_TypeError, "start_func must be callable");
  else if(stop && !PyCallable_Check(stop))
    PyErr_SetString(PyExc_TypeError, "stop_func must be callable");


  if(PyErr_Occurred())
    return NULL;

  self = (pyMM*)type->tp_alloc(type,0);
  self->_mm = nullptr;

  if(self != NULL){
    try{
      mm = new MarketMaker_Py(StartFuncWrap(start),StopFuncWrap(stop),
                              ExecCallbackWrap(cb));
      if(!mm)
        throw std::runtime_error("self->_sob was not constructed");
      else
        self->_mm = (PyObject*)mm;
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

static void MM_Delete(pyMM* self)
{
  if(self->_mm)
    delete (MarketMaker_Py*)(self->_mm);
  Py_TYPE(self)->tp_free((PyObject*)self);
}

PyTypeObject pyMM_type =
{
  PyVarObject_HEAD_INIT(NULL,0)
  "simpleorderbook.MarketMaker",
  sizeof(pyMM),
  0,
  (destructor)MM_Delete,
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
  "MarketMaker (implemented in C++)",
  0,
  0,
  0,
  0,
  0,
  0,
  pyMM_methods,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0, //(initproc)MM_Init,
  0,
  MM_New,
};

#endif /* IGNORE_TO_DEBUG_NATIVE */




