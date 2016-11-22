/*
Copyright (C) 2015 Jonathon Ogden         < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.    If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_0815_COMMON
#define JO_0815_COMMON

#include <Python.h>
#include <structmember.h>
#include "../types.hpp"
#include "../marketmaker.hpp"
#include "../interfaces.hpp"

//#define IGNORE_TO_DEBUG_NATIVE
#ifndef IGNORE_TO_DEBUG_NATIVE

#define MM_RANDOM 1
#define MM_SIMPLE1 2
#define MM_PYOBJ 3

#define SOB_QUARTER_TICK 1
#define SOB_TENTH_TICK 2
#define SOB_THIRTYSECONDTH_TICK 3
#define SOB_HUNDREDTH_TICK 4
#define SOB_THOUSANDTH_TICK 5
#define SOB_TENTHOUSANDTH_TICK 6

/* python/marketmaker_py.cpp */
extern PyTypeObject pyMM_type;

typedef struct {
    PyObject_HEAD
    PyObject* _mm;
    bool _valid;
} pyMM;

typedef struct {
    PyObject_HEAD
    PyObject* _sob;
} pySOB;

class PyFuncWrap {
protected:
    PyObject* _callback;

    PyFuncWrap(PyObject* callback = nullptr)
        : 
            _callback(callback)
        { 
            Py_XINCREF(callback); 
        }

    PyFuncWrap(const PyFuncWrap& obj)
        : 
            _callback(obj._callback)
        { 
            Py_XINCREF(obj._callback); 
        }

public:
    virtual 
    ~PyFuncWrap() 
        { 
            Py_XDECREF(_callback); 
        }

    operator 
    bool()
    { 
        return _callback; 
    }
};


class ExecCallbackWrap
    : public PyFuncWrap {
public:
    ExecCallbackWrap(PyObject* callback = nullptr)
        : 
            PyFuncWrap(callback) 
        {
        }

    ExecCallbackWrap(const ExecCallbackWrap& obj)
        : 
            PyFuncWrap(obj) 
        {
        }

    void 
    operator()(NativeLayer::callback_msg msg, 
               NativeLayer::id_type id,
               NativeLayer::price_type price, 
               NativeLayer::size_type size) const
    {
        PyObject* args = Py_BuildValue("kkfk", (int)msg, id, price, size);
        PyObject_CallObject(_callback, args);
        Py_DECREF(args);
    }
};


class StartFuncWrap
    : public PyFuncWrap {
public:
    StartFuncWrap(PyObject* callback = nullptr)
        : 
            PyFuncWrap(callback) 
        {
        }

    StartFuncWrap(const StartFuncWrap& obj)
        : 
            PyFuncWrap(obj) 
        {
        }

    void 
    operator()(NativeLayer::price_type implied, 
               NativeLayer::price_type tick) const
    {
        PyObject* args = Py_BuildValue("ff", implied, tick);
        PyObject_CallObject(_callback, args);
        Py_DECREF(args);
    }
};


class StopFuncWrap
    : public PyFuncWrap {
public:
    StopFuncWrap(PyObject* callback = nullptr)
        : 
            PyFuncWrap(callback) 
        {
        }

    StopFuncWrap(const StopFuncWrap& obj)
        : 
            PyFuncWrap(obj) 
        {
        }

    void 
    operator()() const 
    { 
        PyObject_CallObject(_callback, NULL); 
    }
};


class MarketMaker_Py
    : public NativeLayer::MarketMaker {

    /* allow python ext to insert directly */
    template<bool BuyNotSell>
    friend PyObject* 
    MM_insert(pyMM* self, PyObject* args, PyObject* kwds);

    StartFuncWrap _start;
    StopFuncWrap _stop;
    ExecCallbackWrap _cb;

    void 
    _exec_callback(NativeLayer::callback_msg msg,
                   NativeLayer::id_type id,
                   NativeLayer::price_type price,
                   NativeLayer::size_type size)
    {
        if(_cb)
            _cb(msg,id,price,size);       
    }

    void 
    start(NativeLayer::SimpleOrderbook::LimitInterface *book,
          NativeLayer::price_type implied, 
          NativeLayer::price_type tick)
    {
        my_base_type::start(book,implied,tick);
        if(_start)
            _start(implied, tick);       
    }

    void 
    stop()
    {
        my_base_type::stop();
        if(_stop)
            _stop();       
    }

    virtual NativeLayer::pMarketMaker 
    _move_to_new()
    {
        return NativeLayer::pMarketMaker( 
                    new MarketMaker_Py(
                        std::move(static_cast<MarketMaker_Py&&>(*this))
                    )
                );
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

    MarketMaker_Py(StartFuncWrap start, StopFuncWrap stop, ExecCallbackWrap cb)
        :
            my_base_type(),
            _start(start),
            _stop(stop),
            _cb(cb)
        {
        }

    virtual 
    ~MarketMaker_Py() noexcept 
        {
        }
  
};

#endif /* IGNORE_TO_DEBUG_NATIVE */

#endif /* JO_0815_COMMON */

