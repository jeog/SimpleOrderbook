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

// stop-limit
template<sob::order_type OT>
bool
get_args(pyAOT_OTO *obj,  PyObject *args, PyObject *kwds, PyObject **is_buy)
{
    static char* kwlist[] = {Strings::is_buy, Strings::stop, Strings::limit,
                             Strings::size, Strings::trigger, NULL};

    return MethodArgs::parse(args, kwds, "Oddk|i", kwlist, is_buy, &obj->stop,
                             &obj->limit, &obj->size, &obj->trigger);
}

// limit
template<>
bool
get_args<sob::order_type::limit>( pyAOT_OTO *obj,
                                  PyObject *args,
                                  PyObject *kwds,
                                  PyObject **is_buy )
{
    static char* kwlist[] = {Strings::is_buy, Strings::limit, Strings::size,
                             Strings::trigger, NULL};

    return MethodArgs::parse(args, kwds, "Odk|i", kwlist, is_buy, &obj->limit,
                             &obj->size, &obj->trigger);
}

// stop
template<>
bool
get_args<sob::order_type::stop>( pyAOT_OTO *obj,
                                 PyObject *args,
                                 PyObject *kwds,
                                 PyObject **is_buy )
{
    static char* kwlist[] = {Strings::is_buy, Strings::stop, Strings::size,
                             Strings::trigger, NULL};

   return MethodArgs::parse(args, kwds, "Odk|i", kwlist, is_buy, &obj->stop,
                            &obj->size, &obj->trigger);
}

// stop
template<>
bool
get_args<sob::order_type::market>( pyAOT_OTO *obj,
                                   PyObject *args,
                                   PyObject *kwds,
                                   PyObject **is_buy )
{
    static char* kwlist[] = {Strings::is_buy, Strings::size, Strings::trigger,
                             NULL};

   return MethodArgs::parse(args, kwds, "Ok|i", kwlist, is_buy, &obj->size,
                            &obj->trigger);
}

/* CONSTRUCTOR */
int
init( pyAOT_OTO *self, PyObject *args, PyObject *kwds )
{
    PyObject *is_buy;
    if( !get_args<sob::order_type::stop_limit>(self, args, kwds, &is_buy) ){
        return -1;
    }

    int b = PyObject_IsTrue(is_buy);
    if( b < 0 ){
        PyErr_SetString(PyExc_TypeError, "'is_buy' not evaluated as bool");
        return -2;
    }
    self->is_buy = static_cast<bool>(b);

    return init_base<sob::AdvancedOrderTicketOTO>(self);
}

/* FACTORY */
template<sob::order_type OT>
PyObject*
build( PyObject *cls, PyObject *args, PyObject *kwds )
{
    pyAOT_OTO *obj = pyAOT_new<pyAOT_OTO>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }

    PyObject *is_buy;
    if( get_args<OT>(obj, args, kwds, &is_buy) ){
        int b = PyObject_IsTrue(is_buy);
        if( b >= 0 ){
            obj->is_buy = static_cast<bool>(b);
            if( init_base<sob::AdvancedOrderTicketOTO>(obj) >= 0 ){
                return reinterpret_cast<PyObject*>(obj);
            }
            PyErr_SetString(PyExc_Exception, "one_triggers_other _init failed");
        }else{
            PyErr_SetString(PyExc_Exception, "'is_buy' not evaluated as bool");
        }
    }

    pyAOT_delete(obj);
    return NULL;
}

PyMethodDef methods[] = {
    BUILD_AOT_METHOD_DEF("build_market", build<sob::order_type::market>,
                         "build market OTO ticket"),
    BUILD_AOT_METHOD_DEF("build_limit", build<sob::order_type::limit>,
                         "build limit OTO ticket" ),
    BUILD_AOT_METHOD_DEF("build_stop", build<sob::order_type::stop>,
                         "build stop OTO ticket" ),
    BUILD_AOT_METHOD_DEF("build_stop_limit", build<sob::order_type::stop_limit>,
                         "build stop-limit OTO ticket" ),
    {NULL}
};

PyMemberDef members[] = {
    BUILD_PY_OBJ_MEMBER_DEF(is_buy, T_BOOL, pyAOT_OTO),
    BUILD_PY_OBJ_MEMBER_DEF(size, T_ULONG, pyAOT_OTO),
    BUILD_PY_OBJ_MEMBER_DEF(limit, T_DOUBLE, pyAOT_OTO),
    BUILD_PY_OBJ_MEMBER_DEF(stop, T_DOUBLE, pyAOT_OTO),
    {NULL}
};

}; /* namespace */


template<>
sob::AdvancedOrderTicket
py_to_native_aot<pyAOT_OTO>(pyAOT_OTO* obj)
{
    auto trigger = static_cast<sob::condition_trigger>(obj->trigger);
    if( obj->limit ){
        if( obj->stop ){
            return sob::AdvancedOrderTicketOTO::build_stop_limit(
                    obj->is_buy, obj->stop, obj->limit, obj->size, trigger
                    );
        }
        return sob::AdvancedOrderTicketOTO::build_limit(
                obj->is_buy, obj->limit, obj->size, trigger
                );
    }else if( obj->stop ){
        return sob::AdvancedOrderTicketOTO::build_stop(
                obj->is_buy, obj->stop, obj->size, trigger
                );
    }
    return sob::AdvancedOrderTicketOTO::build_market(
            obj->is_buy, obj->size, trigger
            );
}


template<>
pyAOT_OTO*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    pyAOT_OTO *obj = pyAOT_new<pyAOT_OTO>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }

    const sob::OrderParamaters* order1 = aot.order1();
    obj->is_buy = order1->is_buy();
    obj->size = order1->size();
    obj->limit = order1->limit_price();
    obj->stop = order1->stop_price();
    return obj;
}


BUILD_AOT_DERIVED_TYPE_OBJ(
        pyAOT_OTO,
        "simpleorderbook.AdvancedOrderTicketOTO",
        "AdvancedOrderTicket one-triggers-other object"
        );

