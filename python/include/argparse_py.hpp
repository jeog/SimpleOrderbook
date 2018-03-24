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

#ifndef JO_SOB_ARGPARSE_PY
#define JO_SOB_ARGPARSE_PY

#include <Python.h>

#include <sstream>
#include <iomanip>

#include "../../include/common.hpp"
#include "common_py.hpp"
#include "strings_py.hpp"

class MethodArgs {
    template<typename T, typename... TArgs>
    static std::string
    build_arg_values_str(T *arg1, TArgs... targs)
    { return to_string(*arg1) + " " + build_arg_values_str(targs...); }

    template<typename T>
    static std::string
    build_arg_values_str(T *arg1)
    { return to_string(*arg1); }

    static std::string
    build_keywords_str(char **kwds);

public:
    template<typename ...TArgs>
    static bool
    parse( PyObject *args,
           PyObject *kwds,
           const char* frmt,
           char **kwlist,
           TArgs... targs )
    {
        if( !PyArg_ParseTupleAndKeywords(args, kwds, frmt, kwlist, targs...) )
        {
            PyErr_SetString(PyExc_ValueError, "error parsing args");
            display_parse_error(std::cerr, frmt, kwlist, targs...);
            return false;
        }
        return true;
    }

    template<typename ...TArgs>
    static bool
    parse(PyObject *args, const char* frmt, TArgs... targs)
    {
        if( !PyArg_ParseTuple(args, frmt, targs...) )
        {
            PyErr_SetString(PyExc_ValueError, "error parsing args");
            display_parse_error(std::cerr, frmt, targs...);
            return false;
        }
        return true;
    }

    template<typename... TArgs>
    static void
    print(std::ostream& out, std::string frmt, char **kw, TArgs... targs)
    {
        out << "format: " << frmt << std::endl
            << "keywords: " << build_keywords_str(kw) << std::endl
            << "values: " << build_arg_values_str(targs...) << std::endl;
    }

    template<typename... TArgs>
    static void
    print(std::ostream& out, std::string frmt, TArgs... targs)
    {
        out << "* format: " << frmt << std::endl
            << "* values: " << build_arg_values_str(targs...) << std::endl;
    }

    template<typename... TArgs>
    static void
    display_parse_error(std::ostream& out, TArgs... args)
    {
        out << "*** parse error ***" << std::endl;
        print(out, args...);
        out << "*** parse error ***" << std::endl;
    }
};


class OrderMethodArgsBase
        : protected MethodArgs {
protected:
    static const std::map<sob::order_type, std::array<char*,7>> keywords;
    static const std::map<sob::order_type, std::string> format_strs;
};


template<bool Replace>
class OrderMethodArgs
        : protected OrderMethodArgsBase {
    static_assert( std::is_same<sob::id_type, unsigned long>::value,
                   "id_type != unsigned long (change frmt strings)" );

protected:
    /* IF compiler gives 'error: no matching function for ... check_args()'
       check order of (id_arg + targs) in caller */
    template<typename T, typename T2, typename ...TArgs>
    static bool
    check_args(T arg1, T2 arg2, TArgs... args) // 4 ???
    { return check_args(arg2, args...); }

    static bool
    check_args(long *size, PyObject **cb, PyObject **advanced)
    {
        if( *cb && !PyCallable_Check(*cb) ){
            PyErr_SetString(PyExc_TypeError,"callback must be callable");
            return false;
        }
        if( *size <= 0 ){
            PyErr_SetString(PyExc_ValueError, "size must be > 0");
            return false;
        }
        return true;
    }

    using MethodArgs::parse;

public:
    template<typename IdTy, typename ...TArgs>
    static bool
    parse( PyObject *args,
           PyObject *kwds,
           sob::order_type ot,
           IdTy id_arg,
           TArgs... targs)
    {
        const char *frmt = format_strs.at(ot).c_str();
        char **kptr = const_cast<char**>(keywords.at(ot).data());
        if( !parse(args, kwds, frmt, kptr, id_arg, targs...) ){
            return false;
        }
        if( !check_args(id_arg, targs...) ){
            display_parse_error(std::cerr, frmt, kptr, targs...);
            return false;
        }
        return true;
    }
};

template<>
class OrderMethodArgs<false>
        : public OrderMethodArgs<true>{
    using OrderMethodArgs<true>::parse;
public:
    template<typename IdTy, typename ...TArgs>
    static bool
    parse( PyObject *args,
           PyObject *kwds,
           sob::order_type ot,
           IdTy id_arg,
           TArgs... targs )
    {
        const char *frmt = format_strs.at(ot).c_str() + 1;
        char **kptr = const_cast<char**>(keywords.at(ot).data()) + 1;
        if( !parse(args, kwds, frmt, kptr, targs...) ){
            return false;
        }
        if( !check_args(targs...) ){
            display_parse_error(std::cerr, frmt, kptr, targs...);
            return false;
        }
        return true;
    }
};

#endif /* JO_SOB_ARGPARSE_PY */
