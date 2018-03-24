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

#ifndef JO_SOB_COMMON_PY
#define JO_SOB_COMMON_PY

#include <Python.h>

#include <sstream>
#include <map>

#include "../../include/simpleorderbook.hpp"

#define BUILD_PY_OBJ_MEMBER_DEF(m, mtyval, oty) \
{Strings::m, mtyval, offsetof(oty, m), READONLY, Strings::m ## _doc}

/* do better than this */
#define CONVERT_AND_THROW_NATIVE_EXCEPTION(e) \
do{ \
    std::stringstream ss; \
    ss <<  typeid(e).name() << ": " << e.what(); \
    PyErr_SetString(PyExc_Exception, ss.str().c_str()); \
    return NULL; \
}while(0)

extern const std::map<int, std::pair<std::string,
                                     sob::DefaultFactoryProxy>> SOB_TYPES;
extern const std::map<int, std::string> ORDER_TYPES;
extern const std::map<int, std::string> CALLBACK_MESSAGES;
extern const std::map<int, std::string> SIDES_OF_MARKET;
extern const std::map<int, std::string> ORDER_CONDITIONS;
extern const std::map<int, std::string> CONDITION_TRIGGERS;

extern volatile bool exiting_pre_finalize;

extern PyTypeObject pySOB_type;
extern PyTypeObject pyOrderInfo_type;

typedef struct {
    PyObject_HEAD
    int order_type;
    bool is_buy;
    double limit;
    double stop;
    size_t size;
    PyObject* advanced;
} pyOrderInfo;

PyObject*
pyOrderInfo_create(const sob::order_info& oi);

struct MDef{
    template<typename F>
    static constexpr PyMethodDef
    NoArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_NOARGS, desc}; }

    template<typename F>
    static constexpr PyMethodDef
    KeyArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_VARARGS | METH_KEYWORDS, desc}; }

    template<typename F>
    static constexpr PyMethodDef
    VarArgs( const char* name, F func, const char* desc )
    { return {name, (PyCFunction)func, METH_VARARGS, desc}; }
};

std::string
to_string(PyObject *arg);

using std::to_string;
#endif /* JO_SOB_COMMON_PY */
