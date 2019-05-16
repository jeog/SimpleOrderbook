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

namespace{
/*
 * Python-C API requires member offsets for custom objects and we
 * require inheritance for advanced order tickets. No guarantee by the
 * standard for non-pod but very unlikely not to be implemented in a way to
 * allow for 'offsetof' macro. We suppress the warnings and manually check.
 */
struct _offset_test
        : public pyAOT{
    uint64_t a;
    uint64_t b;
};
struct _offset_test_2
        : public _offset_test{
    uint64_t c;
};
static_assert( offsetof(_offset_test, a) == sizeof(pyAOT)
               && offsetof(_offset_test, b) == sizeof(pyAOT) + 8
               && offsetof(_offset_test_2, c) == sizeof(_offset_test),
               "*** FATAL - BAD NON-POD OFFSETS ***");


PyMemberDef members[] = {
    BUILD_PY_OBJ_MEMBER_DEF(condition, T_INT, pyAOT),
    BUILD_PY_OBJ_MEMBER_DEF(trigger, T_INT, pyAOT),
    {NULL}
};

bool
get_args(pyAOT *obj,  PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {Strings::condition, Strings::trigger, NULL};
    return MethodArgs::parse(args, kwds, "ii", kwlist, &obj->condition,
                             &obj->trigger);
}

/* CONSTRUCTOR */
int
init(pyAOT *self, PyObject *args, PyObject *kwds)
{
    if( !get_args(self,args,kwds) ){
        return -1;
    }

    if( ORDER_CONDITIONS.find(self->condition) == ORDER_CONDITIONS.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid order condition");
        return -1;
    }

    if( CONDITION_TRIGGERS.find(self->trigger) == CONDITION_TRIGGERS.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid condition trigger");
        return -1;
    }
    return 0;
}

}; /* namespace */


PyObject*
pyAOT_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self = (PyObject*)type->tp_alloc(type,0);
    if( !self ){
        PyErr_SetString(PyExc_MemoryError, "tp_alloc failed for AOT object");
    }
    return self;
}


void
pyAOT_delete(pyAOT *self)
{ Py_TYPE(self)->tp_free((PyObject*)self); }


template<>
sob::AdvancedOrderTicket
py_to_native_aot<PyObject>(PyObject* obj)
{
    if( !obj ){
        return sob::AdvancedOrderTicket::null;
    }
    uintptr_t addr = reinterpret_cast<uintptr_t>(obj->ob_type);
    if( addr == reinterpret_cast<uintptr_t>(&pyAOT_OCO_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_OCO*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_OTO_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_OTO*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_FOK_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_FOK*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_BRACKET_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_BRACKET*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_TrailingStop_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_TrailingStop*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_TrailingBracket_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_TrailingBracket*>(obj));
    }else if( addr == reinterpret_cast<uintptr_t>(&pyAOT_AON_type) ){
        return py_to_native_aot(reinterpret_cast<pyAOT_AON*>(obj));
    }else{
        throw std::runtime_error("invalid pyAOT type");
    }
}


template<>
PyObject*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    pyAOT* obj;
    /*
     * '_active' versions are special cases: we don't handle initial
     * advanced order info properly in advanced.tpp:_bndl_to_aot so we
     * do our best to replicate what _bndl_to_aot does
     */
    switch( aot.condition() ){
    case sob::order_condition::one_cancels_other:
        obj = native_aot_to_py<pyAOT_OCO>(aot);
        break;
    case sob::order_condition::one_triggers_other:
        obj = native_aot_to_py<pyAOT_OTO>(aot);
        break;
    case sob::order_condition::fill_or_kill:
        obj = native_aot_to_py<pyAOT_FOK>(aot);
        break;
    case sob::order_condition::bracket:
        obj = native_aot_to_py<pyAOT_BRACKET>(aot);
        break;
    case sob::order_condition::trailing_stop:
        obj = native_aot_to_py<pyAOT_TrailingStop>(aot);
        break;
    case sob::order_condition::trailing_bracket:
        obj = native_aot_to_py<pyAOT_TrailingBracket>(aot);
        break;
    case sob::order_condition::_bracket_active:
        obj = native_aot_to_py<pyAOT_BRACKET_Active>(aot);
        break;
    case sob::order_condition::_trailing_bracket_active:
        obj = native_aot_to_py<pyAOT_TrailingBracket_Active>(aot);
        break;
    case sob::order_condition::_trailing_stop_active:
        obj = pyAOT_new<pyAOT_TrailingStop_Active>();
        break;
    case sob::order_condition::all_or_nothing:
        obj = native_aot_to_py<pyAOT_AON>(aot);
        break;
    default:
        return nullptr;
    };

    obj->condition = static_cast<int>( aot.condition() );
    obj->trigger = static_cast<int>( aot.trigger() );
    return reinterpret_cast<PyObject*>(obj);
}


BUILD_AOT_DERIVED_TYPE_OBJ_EX(
        pyAOT, (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE),
        0, members, init, 0,
        "simpleorderbook.AdvancedOrderTicket",
        "AdvancedOrderTicket base object"
        );

/* pyAOT_TrailingStop_Active created internally, doesn't define new/init */
BUILD_AOT_DERIVED_TYPE_OBJ_EX(
        pyAOT_TrailingStop_Active, Py_TPFLAGS_DEFAULT,
        0, 0, 0, &pyAOT_type,
        "simpleorderbook.AdvancedOrderTicketTrailingStop_Active",
        "AdvancedOrderTicket (active) trailing-stop object"
        );
