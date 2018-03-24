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

#include <Python.h>
#include <structmember.h>

#include "../include/common_py.hpp"
#include "../include/strings_py.hpp"
#include "../include/advanced_order_py.hpp"

/*
 *  pyOrderInfo is allocated/constructed internally, directly from
 *  orderbook_py.cpp:SOB_get_order_info(...) via pyOrderInfo_create
 *
 *  (We don't define new/init in the type object; but we do define delete)
 */

PyObject*
pyOrderInfo_create(const sob::order_info& oi)
{
    pyOrderInfo *obj = (pyOrderInfo*)pyOrderInfo_type.tp_alloc(&pyOrderInfo_type,0);
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "tp_alloc failed for OrderInfo object");
        return NULL;
    }

    obj->order_type = static_cast<int>(oi.type);
    obj->is_buy = oi.is_buy;
    obj->limit = oi.limit;
    obj->stop = oi.stop;
    obj->size = oi.size;

    obj->advanced = oi.advanced
                  ? native_aot_to_py<PyObject>(oi.advanced)
                  : Py_None;
    Py_INCREF(obj->advanced);

    return reinterpret_cast<PyObject*>(obj);
}

namespace{

void
pyOrderInfo_delete(pyOrderInfo *self)
{
    Py_XDECREF(self->advanced);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

PyMemberDef members[] = {
    BUILD_PY_OBJ_MEMBER_DEF(order_type, T_INT, pyOrderInfo),
    BUILD_PY_OBJ_MEMBER_DEF(is_buy, T_BOOL, pyOrderInfo),
    BUILD_PY_OBJ_MEMBER_DEF(limit, T_DOUBLE, pyOrderInfo),
    BUILD_PY_OBJ_MEMBER_DEF(stop, T_DOUBLE, pyOrderInfo),
    BUILD_PY_OBJ_MEMBER_DEF(size, T_ULONG, pyOrderInfo),
    BUILD_PY_OBJ_MEMBER_DEF(advanced, T_OBJECT, pyOrderInfo),
    {NULL}
};

} /* namespace */

PyTypeObject pyOrderInfo_type = {
    PyVarObject_HEAD_INIT(NULL,0)
    "simpleorderbook.OrderInfo",
    sizeof(pyOrderInfo),
    0,
    (destructor)pyOrderInfo_delete,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    Py_TPFLAGS_DEFAULT,
    "order information" ,
    0, 0, 0, 0, 0, 0, 0,
    members,
    0, 0, 0, 0, 0, 0, 0, 0, 0
};


