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

#ifndef JO_SOB_CALLBACK_PY
#define JO_SOB_CALLBACK_PY

#include "../../include/common.hpp"
#include "common_py.hpp"

#ifndef IGNORE_TO_DEBUG_NATIVE

class PyFuncWrap {
protected:
    PyObject *const cb;

    explicit PyFuncWrap(PyObject *callback);

    PyFuncWrap(const PyFuncWrap& obj);

    inline bool
    valid_interpreter_state() const
    { return !exiting_pre_finalize; }

public:
    virtual
    ~PyFuncWrap();

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
               size_t size) const;
};

inline sob::order_exec_cb_type
wrap_cb(PyObject *cb)
{ return cb ? sob::order_exec_cb_type(ExecCallbackWrap(cb))
            : sob::order_exec_cb_type(); }

#endif /* IGNORE_TO_DEBUG_NATIVE */

#endif
