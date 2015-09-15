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

#ifndef JO_0815_COMMON
#define JO_0815_COMMON

#include <Python.h>
#include <structmember.h>
#include "../types.hpp"
#include "../market_maker.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE

/* python/marketmaker.cpp */
extern PyTypeObject pyMM_type;

typedef struct {
  PyObject_HEAD
  PyObject* _mm;
} pyMM;

typedef struct {
  PyObject_HEAD
  PyObject* _sob;
} pySOB;

class MarketMaker_Py
    : public NativeLayer::MarketMaker {

  template<bool BuyNotSell>
  friend PyObject* MM_insert(pyMM* self, PyObject* args, PyObject* kwds);
  friend PyObject* VOB_add_market_makers_local(pySOB* self, PyObject* args);

public:
  /* easier to use ptrs than function objects for Py_INCR/DECR */
  typedef void(*start_type)(NativeLayer::price_type,NativeLayer::price_type);
  typedef void(*stop_type)(void);
  typedef void(*callback_type)(int,NativeLayer::id_type,NativeLayer::price_type,
                               NativeLayer::size_type);
private:
  start_type _start;
  stop_type  _stop;
  callback_type _cb;
  void _exec_callback(NativeLayer::callback_msg msg,NativeLayer::id_type id,
                      NativeLayer::price_type price,NativeLayer::size_type size)
  {
    this->_cb((int)msg,id,price,size);
  }
  void start(NativeLayer::MarketMaker::sob_iface_type *book,
             NativeLayer::price_type implied, NativeLayer::price_type tick)
  {
    my_base_type::start(book,implied,tick);
    this->_start(implied, tick);
  }
  void stop()
  {
    my_base_type::stop();
    this->_stop();
  }
  virtual NativeLayer::pMarketMaker _move_to_new()
 {
   return NativeLayer::pMarketMaker( new MarketMaker_Py(
     std::move(dynamic_cast<MarketMaker_Py&&>(*this))));
 }

public:
  MarketMaker_Py(MarketMaker_Py&& mm) noexcept
    :
      my_base_type(std::move(mm)),
      _start(mm._start),
      _stop(mm._stop),
      _cb(mm._cb)
    {
    }
  MarketMaker_Py(start_type start, stop_type stop,
                 callback_type cb)
    :
      my_base_type(),
      _start(start),
      _stop(stop),
      _cb(cb)
    {
      Py_XINCREF((PyObject*)(this->_start));
      Py_XINCREF((PyObject*)(this->_stop));
      Py_XINCREF((PyObject*)(this->_cb));
    }
  virtual ~MarketMaker_Py() noexcept
    {
      Py_XDECREF((PyObject*)(this->_start));
      Py_XDECREF((PyObject*)(this->_stop));
      Py_XDECREF((PyObject*)(this->_cb));
    }

};

#endif /* IGNORE_TO_DEBUG_NATIVE */

#endif /* JO_0815_COMMON */

