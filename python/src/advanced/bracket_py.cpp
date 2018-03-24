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

template<int Nargs>
bool
get_args(pyAOT_BRACKET *obj,  PyObject *args, PyObject *kwds, PyObject **is_buy)
{
    static char* kwlist[] = {Strings::is_buy, Strings::loss_stop,
                             Strings::loss_limit, Strings::target_limit,
                             Strings::size, Strings::trigger, NULL};

    return MethodArgs::parse(args, kwds, "Odddk|i", kwlist, is_buy,
                             &obj->loss_stop, &obj->loss_limit,
                             &obj->target_limit, &obj->size, &obj->trigger);
}

template<>
bool
get_args<3>(pyAOT_BRACKET *obj,  PyObject *args, PyObject *kwds, PyObject **is_buy)
{
    static char* kwlist[] = {Strings::loss_stop, Strings::loss_limit,
                             Strings::target_limit, Strings::size,
                             Strings::trigger, NULL};

    return MethodArgs::parse(args, kwds, "dddk|i", kwlist, &obj->loss_stop,
                             &obj->loss_limit, &obj->target_limit,
                             &obj->size, &obj->trigger);
}


template<>
bool
get_args<2>(pyAOT_BRACKET *obj, PyObject *args, PyObject *kwds, PyObject **is_buy)
{
    static char* kwlist[] = {Strings::loss_stop, Strings::target_limit,
                             Strings::size, Strings::trigger,  NULL};

    return MethodArgs::parse(args, kwds, "ddk|i", kwlist, &obj->loss_stop,
                             &obj->target_limit, &obj->size, &obj->trigger);
}

/* CONSTRUCTOR */
int
init( pyAOT_BRACKET *self, PyObject *args, PyObject *kwds )
{
    PyObject *is_buy;
    if( !get_args<4>(self, args, kwds, &is_buy) ){
        return -1;
    }

    int b = PyObject_IsTrue(is_buy);
    if( b < 0 ){
        PyErr_SetString(PyExc_TypeError, "'is_buy' not evaluated as bool");
        return -2;
    }
    self->is_buy = static_cast<bool>(b);

    int ret = init_base<sob::AdvancedOrderTicketBRACKET>(self);
    if( ret < 0 ){
        PyErr_SetString(PyExc_Exception, "bracket _init failed");
    }
    return ret;
}

/* FACTORY */
template<int Nargs, bool IsBuy>
PyObject*
build( PyObject *cls, PyObject *args, PyObject *kwds )
{
    pyAOT_BRACKET *obj = pyAOT_new<pyAOT_BRACKET>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }
    if( !get_args<Nargs>(obj, args, kwds, 0)
        || init_base<sob::AdvancedOrderTicketBRACKET>(obj) < 0 )
    {
        pyAOT_delete(obj);
        return NULL;
    }
    obj->is_buy = IsBuy;
    return reinterpret_cast<PyObject*>(obj);
}

PyMethodDef methods[] = {
    BUILD_AOT_METHOD_DEF("build_sell_stop_limit", (build<3,false>),
                         "build sell-stop-limit bracket ticket" ),
    BUILD_AOT_METHOD_DEF("build_sell_stop", (build<2,false>),
                         "build sell-stop bracket ticket" ),
    BUILD_AOT_METHOD_DEF("build_buy_stop_limit", (build<3,true>),
                         "build buy-stop-limit bracket ticket" ),
    BUILD_AOT_METHOD_DEF("build_buy_stop", (build<2,true>),
                         "build buy-stop bracket ticket" ),
    {NULL}
};

PyMemberDef members[] = {
    BUILD_PY_OBJ_MEMBER_DEF(is_buy, T_BOOL, pyAOT_BRACKET),
    BUILD_PY_OBJ_MEMBER_DEF(size, T_ULONG, pyAOT_BRACKET),
    BUILD_PY_OBJ_MEMBER_DEF(loss_limit, T_DOUBLE, pyAOT_BRACKET),
    BUILD_PY_OBJ_MEMBER_DEF(loss_stop, T_DOUBLE, pyAOT_BRACKET),
    BUILD_PY_OBJ_MEMBER_DEF(target_limit, T_DOUBLE, pyAOT_BRACKET),
    {NULL}
};

}; /* namespace */

template<>
sob::AdvancedOrderTicket
py_to_native_aot<pyAOT_BRACKET>(pyAOT_BRACKET* obj)
{
    double stop = obj->loss_stop;
    double target = obj->target_limit;
    size_t sz = obj->size;
    auto trigger = static_cast<sob::condition_trigger>(obj->trigger);

    if( obj->loss_limit ){
        auto func = obj->is_buy
                  ? sob::AdvancedOrderTicketBRACKET::build_buy_stop_limit
                  : sob::AdvancedOrderTicketBRACKET::build_sell_stop_limit;
        return func(stop, obj->loss_limit, target, sz, trigger);
    }
    auto func = obj->is_buy
              ? sob::AdvancedOrderTicketBRACKET::build_buy_stop
              : sob::AdvancedOrderTicketBRACKET::build_sell_stop;
    return func(stop, target, sz, trigger);
}

template<>
pyAOT_BRACKET*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    pyAOT_BRACKET *obj = pyAOT_new<pyAOT_BRACKET>();
    if( !obj ){
        PyErr_SetString(PyExc_MemoryError, "order ticket allocation failed");
        return NULL;
    }

    const sob::OrderParamaters* order1 = aot.order1();
    obj->is_buy = order1->is_buy();
    obj->size = order1->size();
    obj->loss_limit = order1->limit_price();
    obj->loss_stop = order1->stop_price();
    obj->target_limit = aot.order2()->limit_price();
    return obj;
}

BUILD_AOT_DERIVED_TYPE_OBJ(
        pyAOT_BRACKET,
        "simpleorderbook.AdvancedOrderTicketBRACKET",
        "AdvancedOrderTicket bracket object"
        );
