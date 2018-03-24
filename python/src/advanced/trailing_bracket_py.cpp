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

#include <map>

#include "../../include/common_py.hpp"
#include "../../include/advanced_order_py.hpp"
#include "../../include/argparse_py.hpp"

namespace {

bool
get_args(pyAOT_TrailingBracket *obj,  PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::stop_nticks, Strings::target_nticks, NULL};

    return MethodArgs::parse(args, kwds, "kk", kwlist, &obj->stop_nticks,
                             &obj->target_nticks);
}

int
init(pyAOT_TrailingBracket *self, PyObject *args, PyObject *kwds)
{
    if( !get_args(self, args, kwds) ){
        return -1;
    }
    return init_base<sob::AdvancedOrderTicketTrailingBracket>(self);
}

PyObject*
build( PyObject *cls, PyObject *args, PyObject *kwds )
{
    pyAOT_TrailingBracket *obj = pyAOT_new<pyAOT_TrailingBracket>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }
    if( !get_args(obj, args, kwds)
        || init_base<sob::AdvancedOrderTicketTrailingBracket>(obj) < 0 )
    {
        pyAOT_delete(obj);
        return NULL;
    }

    return reinterpret_cast<PyObject*>(obj);
}

PyMethodDef methods[] = {
    BUILD_AOT_METHOD_DEF("build", build, "build trailing bracket ticket" ),
    {NULL}
};

PyMemberDef members[] = {
    BUILD_PY_OBJ_MEMBER_DEF(stop_nticks, T_ULONG, pyAOT_TrailingBracket),
    BUILD_PY_OBJ_MEMBER_DEF(target_nticks, T_ULONG, pyAOT_TrailingBracket),
    {NULL}
};


}; /* namespace */


template<>
sob::AdvancedOrderTicket
py_to_native_aot<pyAOT_TrailingBracket>(pyAOT_TrailingBracket* obj)
{
    return sob::AdvancedOrderTicketTrailingBracket::build(
                    obj->stop_nticks, obj->target_nticks
                    );
}


template<>
pyAOT_TrailingBracket*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    pyAOT_TrailingBracket *obj = pyAOT_new<pyAOT_TrailingBracket>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }

    obj->stop_nticks = aot.order1()->stop_nticks();
    obj->target_nticks = aot.order2()->limit_nticks();
    return obj;
}


BUILD_AOT_DERIVED_TYPE_OBJ(
    pyAOT_TrailingBracket ,
    "simpleorderbook.AdvancedOrderTicketTrailingBracket",
    "AdvancedOrderTicket trailing bracket object"
    );
