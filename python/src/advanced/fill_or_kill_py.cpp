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
get_args(pyAOT_FOK *obj,  PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::trigger, NULL};
    return MethodArgs::parse(args, kwds, "|i", kwlist, &obj->trigger);
}

int
init(pyAOT_FOK *self, PyObject *args, PyObject *kwds)
{
    if( get_args(self, args, kwds) ){
        return init_base<sob::AdvancedOrderTicketFOK>(self);
    }
    return -1;
}

PyObject*
build( PyObject *cls, PyObject *args, PyObject *kwds )
{
    pyAOT_FOK *obj = pyAOT_new<pyAOT_FOK>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }
    if( !get_args(obj, args, kwds)
        || init_base<sob::AdvancedOrderTicketFOK>(obj) < 0 )
    {
        pyAOT_delete(obj);
        return NULL;
    }
    return reinterpret_cast<PyObject*>(obj);
}

PyMethodDef methods[] = {
    BUILD_AOT_METHOD_DEF("build", build, "build FOK ticket" ),
    {NULL}
};

PyMemberDef members[] = {
    {NULL}
};

}; /* namespace */


template<>
sob::AdvancedOrderTicket
py_to_native_aot<pyAOT_FOK>(pyAOT_FOK* obj)
{
    return sob::AdvancedOrderTicketFOK::build(
            static_cast<sob::condition_trigger>(obj->trigger)
            );
}

template<>
pyAOT_FOK*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    pyAOT_FOK *obj = pyAOT_new<pyAOT_FOK>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }
    return obj;
}


BUILD_AOT_DERIVED_TYPE_OBJ(
        pyAOT_FOK ,
        "simpleorderbook.AdvancedOrderTicketFOK",
        "AdvancedOrderTicket fill-or-kill object"
        );

