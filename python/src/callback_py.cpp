/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#include "../../include/common.hpp"
#include "../../include/simpleorderbook.hpp"
#include "../include/callback_py.hpp"

#ifndef IGNORE_TO_DEBUG_NATIVE

PyFuncWrap::PyFuncWrap(PyObject *callback)
    :
        cb(callback)
    {
        if( valid_interpreter_state() ){
            PyGILState_STATE gs = PyGILState_Ensure();
            Py_XINCREF(callback);
            PyGILState_Release(gs);
        }
    }

PyFuncWrap::PyFuncWrap(const PyFuncWrap& obj)
    :
        cb(obj.cb)
    {
        if( valid_interpreter_state() ){
            PyGILState_STATE gs = PyGILState_Ensure();
            Py_XINCREF(obj.cb);
            PyGILState_Release(gs);
        }
    }

PyFuncWrap::~PyFuncWrap()
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

void
ExecCallbackWrap::operator()( sob::callback_msg msg,
                              sob::id_type id1,
                              sob::id_type id2,
                              double price,
                              size_t size) const
{
    if( !(*this) ){
        return;
    }
    /* see notes in destructor about this check */
    if( !valid_interpreter_state() ){
        std::cerr<< "* callback(" << std::hex << reinterpret_cast<void*>(cb)
                 << ") not called; invalid interpreter state *"
                 << std::dec << std::endl;
        return;
    }
    /* ignore race condition here for same reason as in destructor */
    PyGILState_STATE gs = PyGILState_Ensure();
    PyObject *args = Py_BuildValue("kkkdk",
            static_cast<int>(msg), id1, id2, price, size);
    PyObject* res = PyObject_CallObject(cb, args);
    if( PyErr_Occurred() ){
        std::cerr<< "* callback(" << std::hex << reinterpret_cast<void*>(cb)
                 <<  ") error * " << std::dec << std::endl;
        PyErr_Print();
    }
    Py_XDECREF(res);
    Py_XDECREF(args);
    PyGILState_Release(gs);
}

#endif /* IGNORE_TO_DEBUG_NATIVE */


