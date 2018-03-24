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

#ifndef JO_SOB_ADVANCED_PY
#define JO_SOB_ADVANCED_PY

#include <Python.h>
#include <structmember.h>

#include "../../include/common.hpp"
#include "../../include/simpleorderbook.hpp"
#include "common_py.hpp"

// TODO fix the 'Active' pyAOT versions (need to fix _bndl_to_aot first)

/* base ticket */
typedef struct {
    PyObject_HEAD
    int condition;
    int trigger;
} pyAOT;

/* one-cancels-other */
struct pyAOT_OCO
        : public pyAOT {
    bool is_buy;
    size_t size;
    double limit;
    double stop;
};

/* one-triggers-other */
struct pyAOT_OTO
        : public pyAOT {
    bool is_buy;
    size_t size;
    double limit;
    double stop;
};

/* fill-or-kill */
struct pyAOT_FOK
        : public pyAOT {
};

/* bracket */
struct pyAOT_BRACKET
        : public pyAOT {
    bool is_buy;
    size_t size;
    double loss_limit;
    double loss_stop;
    double target_limit;
};

/* trailing stop */
struct pyAOT_TrailingStop
        : public pyAOT {
    size_t nticks;
};

/* trailing bracket */
struct pyAOT_TrailingBracket
        : public pyAOT {
    size_t stop_nticks;
    size_t target_nticks;
};

/* (active) bracket */
struct pyAOT_BRACKET_Active
        : public pyAOT_OCO {
};

/* (active) trailing bracket */
struct pyAOT_TrailingBracket_Active
        : public pyAOT_OCO {
};

/* (active) trailing stop */
struct pyAOT_TrailingStop_Active
        : public pyAOT {
};

#define BUILD_AOT_METHOD_DEF(m, f, doc) \
{m, (PyCFunction)f, (METH_VARARGS | METH_KEYWORDS | METH_CLASS), doc}

/* base type (defined in advanced_order_py.cpp) */
extern PyTypeObject pyAOT_type;

/* the derived type objects (defined in each advanced source file via macro) */
extern PyTypeObject pyAOT_OCO_type;
extern PyTypeObject pyAOT_OTO_type;
extern PyTypeObject pyAOT_FOK_type;
extern PyTypeObject pyAOT_BRACKET_type;
extern PyTypeObject pyAOT_TrailingStop_type;
extern PyTypeObject pyAOT_TrailingBracket_type;
extern PyTypeObject pyAOT_BRACKET_Active_type;
extern PyTypeObject pyAOT_TrailingStop_Active_type;
extern PyTypeObject pyAOT_TrailingBracket_Active_type;

#define BUILD_AOT_DERIVED_TYPE_OBJ_EX(obj, flags, meths, mmbrs, cnstr, base, name, doc) \
PyTypeObject obj ## _type = { \
    PyVarObject_HEAD_INIT(NULL,0) \
    name, \
    sizeof(obj), \
    0, \
    (destructor)pyAOT_delete, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    flags, \
    doc, \
    0, 0, 0, 0, 0, 0, \
    meths, \
    mmbrs, \
    0, \
    base, \
    0, 0, 0, 0, \
    (initproc)cnstr, \
    0, \
    (PyObject*(*)(PyTypeObject*, PyObject*, PyObject*))pyAOT_new \
}

#define BUILD_AOT_DERIVED_TYPE_OBJ(obj, name, doc) \
        BUILD_AOT_DERIVED_TYPE_OBJ_EX(obj, Py_TPFLAGS_DEFAULT, methods, \
                                      members, init, &pyAOT_type,name, doc)

/* 'new' that is called directly by python, indirectly by us */
PyObject*
pyAOT_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

/* map our ticket types to their (Python-C) type objects */
template<typename T>
constexpr PyTypeObject*
pyAOT_type_obj()
{
    static_assert( std::is_base_of<pyAOT,T>::value, "pyAOT type not base of T");
    return
      std::is_same<T, pyAOT>::value ? &pyAOT_type
    : std::is_same<T, pyAOT_TrailingStop_Active>::value
          ? &pyAOT_TrailingStop_Active_type
    : std::is_same<T, pyAOT_OCO>::value ? &pyAOT_OCO_type
    : std::is_same<T, pyAOT_BRACKET_Active>:: value
          ? &pyAOT_BRACKET_Active_type
    : std::is_same<T, pyAOT_TrailingBracket_Active>::value
          ? &pyAOT_TrailingBracket_Active_type
    : std::is_same<T, pyAOT_OTO>::value ? &pyAOT_OTO_type
    : std::is_same<T, pyAOT_FOK>::value ? &pyAOT_FOK_type
    : std::is_same<T, pyAOT_BRACKET>::value ? &pyAOT_BRACKET_type
    : std::is_same<T, pyAOT_TrailingStop>::value ? &pyAOT_TrailingStop_type
    : std::is_same<T, pyAOT_TrailingBracket>::value ? &pyAOT_TrailingBracket_type
    : throw std::runtime_error("invalid pyAOT type");
}

/* 'new that is called directly by us */
template<typename T>
inline T*
pyAOT_new()
{ return reinterpret_cast<T*>(pyAOT_new(pyAOT_type_obj<T>(), 0, 0)); }

void
pyAOT_delete(pyAOT *self);

template<typename AOTClass>
int
init_base(pyAOT *self)
{
    if( !self->trigger ){
        self->trigger = static_cast<int>(AOTClass::default_trigger);
    }
    if( CONDITION_TRIGGERS.find(self->trigger) == CONDITION_TRIGGERS.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid conditional trigger");
        return -1;
    }
    self->condition = static_cast<int>(AOTClass::condition);
    return 0;
}

/* type checks, see specializations for implementation */
template<typename T>
sob::AdvancedOrderTicket
py_to_native_aot(T* obj)
{
    static_assert( std::is_base_of<pyAOT, T>::value
                   || std::is_same<PyObject, T>::value, "invalid type");
    return sob::AdvancedOrderTicket::null;
}

/* type checks, see specializations for implementation */
template<typename T>
T*
native_aot_to_py(const sob::AdvancedOrderTicket& aot)
{
    static_assert( std::is_base_of<pyAOT, T>::value
                   || std::is_same<PyObject, T>::value, "invalid type");
    return nullptr;
}

#endif /* JO_SOB_ADVANCED_PY */
