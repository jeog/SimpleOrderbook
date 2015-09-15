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

#include "../market_maker.hpp"
#include "common.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE



static char keywords[][20] = {"limit", "size", "no_order_cb",
                              "start_func", "stop_func", "exec_callback_func"};

template<bool BuyNotSell>
PyObject* MM_insert(pyMM* self, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;
  price_type limit;
  size_type size;
  bool no_order_cb;

  static char* kwlist[] = {keywords[0],keywords[1],keywords[2],NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "fkb", kwlist, &limit, &size,
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
  {"insert_buy",(PyCFunction)MM_insert<true>, METH_VARARGS, "[to do]"},
  {"insert_sell",(PyCFunction)MM_insert<false>, METH_VARARGS, "[to do]"},
  {NULL}
};


static PyObject* MM_New(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  using namespace NativeLayer;

  pyMM* self;
  MarketMaker_Py* mm;
  PyObject *start, *stop, *cb;

  static char* kwlist[] = {keywords[3],keywords[4],keywords[5],NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "O:callbackO:callbackO:callback",
                                  kwlist, &start, &stop, &cb))
  {
    PyErr_SetString(PyExc_ValueError, "error parsing args to __new__");
    return NULL;
  }

  self = (pyMM*)type->tp_alloc(type,0);
  self->_mm = nullptr;

  if(self != NULL){
    try{
      mm = new MarketMaker_Py((MarketMaker_Py::start_type)start,
                              (MarketMaker_Py::stop_type)stop,
                              (MarketMaker_Py::callback_type)cb);
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




