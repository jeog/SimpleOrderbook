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

#ifndef IGNORE_TO_DEBUG_NATIVE

/* do better than this */
#define CONVERT_AND_THROW_NATIVE_EXCEPTION(e) \
do{ \
    std::stringstream ss; \
    ss <<  typeid(e).name() << ": " << e.what(); \
    PyErr_SetString(PyExc_Exception, ss.str().c_str()); \
    return NULL; \
}while(0)

extern volatile bool exiting_pre_finalize;

std::string
to_string(PyObject *arg);

using std::to_string;

#endif /* IGNORE_TO_DEBUG_NATIVE */

#endif
