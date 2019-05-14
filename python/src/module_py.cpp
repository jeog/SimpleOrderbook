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

#include "../include/common_py.hpp"
#include "../include/argparse_py.hpp"
#include "../include/advanced_order_py.hpp"
#include "../include/strings_py.hpp"

#include <string>
#include <iostream>


template<typename T>
constexpr std::pair<int, std::pair<std::string, sob::DefaultFactoryProxy>>
sob_type_make_entry(int index, std::string name)
{
    return std::make_pair(index,
               std::make_pair(name,
                   sob::SimpleOrderbook::BuildFactoryProxy<T>()) );
}

const std::map<int, std::pair<std::string, sob::DefaultFactoryProxy>>
SOB_TYPES = {
    sob_type_make_entry<sob::quarter_tick>(1, "SOB_QUARTER_TICK"),
    sob_type_make_entry<sob::tenth_tick>(2, "SOB_TENTH_TICK"),
    sob_type_make_entry<sob::thirty_secondth_tick>(3, "SOB_THIRTY_SECONDTH_TICK"),
    sob_type_make_entry<sob::hundredth_tick>(4, "SOB_HUNDREDTH_TICK"),
    sob_type_make_entry<sob::thousandth_tick>(5, "SOB_THOUSANDTH_TICK"),
    sob_type_make_entry<sob::ten_thousandth_tick>(6, "SOB_TEN_THOUSANDTH_TICK")
};

const std::map<int, std::string>
ORDER_TYPES = {
    {static_cast<int>(sob::order_type::null), "ORDER_TYPE_NULL"},
    {static_cast<int>(sob::order_type::market), "ORDER_TYPE_MARKET"},
    {static_cast<int>(sob::order_type::limit), "ORDER_TYPE_LIMIT"},
    {static_cast<int>(sob::order_type::stop), "ORDER_TYPE_STOP"},
    {static_cast<int>(sob::order_type::stop_limit), "ORDER_TYPE_STOP_LIMIT"},
};

const std::map<int, std::string>
CALLBACK_MESSAGES = {
    {static_cast<int>(sob::callback_msg::cancel), "MSG_CANCEL"},
    {static_cast<int>(sob::callback_msg::fill), "MSG_FILL"},
    {static_cast<int>(sob::callback_msg::stop_to_limit), "MSG_STOP_TO_LIMIT"},
    {static_cast<int>(sob::callback_msg::stop_to_market), "MSG_STOP_TO_MARKET"},
    {static_cast<int>(sob::callback_msg::trigger_OCO), "MSG_TRIGGER_OCO"},
    {static_cast<int>(sob::callback_msg::trigger_OTO), "MSG_TRIGGER_OTO"},
    {static_cast<int>(sob::callback_msg::trigger_BRACKET_open), "MSG_TRIGGER_BRACKET_OPEN"},
    {static_cast<int>(sob::callback_msg::trigger_BRACKET_close), "MSG_TRIGGER_BRACKET_CLOSE"},
    {static_cast<int>(sob::callback_msg::trigger_trailing_stop), "MSG_TRIGGER_TRAILING_STOP"},
    {static_cast<int>(sob::callback_msg::adjust_trailing_stop), "MSG_ADJUST_TRAILING_STOP"},
    {static_cast<int>(sob::callback_msg::kill), "MSG_KILL"}
};

const std::map<int, std::string>
SIDES_OF_MARKET = {
    {static_cast<int>(sob::side_of_market::bid), "SIDE_BID"},
    {static_cast<int>(sob::side_of_market::ask), "SIDE_ASK"},
    {static_cast<int>(sob::side_of_market::both), "SIDE_BOTH"}
};

const std::map<int, std::string>
ORDER_CONDITIONS = {
    {static_cast<int>(sob::order_condition::one_cancels_other), "CONDITION_OCO"},
    {static_cast<int>(sob::order_condition::one_triggers_other), "CONDITION_OTO"},
    {static_cast<int>(sob::order_condition::fill_or_kill), "CONDITION_FOK"},
    {static_cast<int>(sob::order_condition::bracket), "CONDITION_BRACKET"},
    {static_cast<int>(sob::order_condition::trailing_stop), "CONDITION_TRAILING_STOP"},
    {static_cast<int>(sob::order_condition::trailing_bracket), "CONDITION_TRAILING_BRACKET"},
    {static_cast<int>(sob::order_condition::_bracket_active), "CONDITION_BRACKET_ACTIVE"},
    {static_cast<int>(sob::order_condition::_trailing_bracket_active), "CONDITION_TRAILING_BRACKET_ACTIVE"},
    {static_cast<int>(sob::order_condition::_trailing_stop_active), "CONDITION_TRAILING_STOP_ACTIVE"},
    {static_cast<int>(sob::order_condition::all_or_nothing), "CONDITION_AON"}
};

const std::map<int, std::string>
CONDITION_TRIGGERS = {
    {static_cast<int>(sob::condition_trigger::fill_partial), "TRIGGER_FILL_PARTIAL"},
    {static_cast<int>(sob::condition_trigger::fill_full), "TRIGGER_FILL_FULL"},
    {static_cast<int>(sob::condition_trigger::none), "TRIGGER_NONE"}
};


namespace {

#ifdef DEBUG
const bool is_debug_build = true;
#else
const bool is_debug_build = false;
#endif /* DEBUG */


PyObject*
tick_size(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::sob_type, NULL};

    int sobty;

    if( !MethodArgs::parse(args, kwds, "i", kwlist, &sobty) ){
        return NULL;
    }

    auto sobty_entry = SOB_TYPES.find(sobty);
    if( sobty_entry == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return NULL;
    }

    return PyFloat_FromDouble( sobty_entry->second.second.tick_size() );
}


PyObject*
price_to_tick(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::sob_type, Strings::price, NULL};

    int sobty;
    double price;

    if( !MethodArgs::parse(args, kwds, "id", kwlist, &sobty, &price) ){
        return NULL;
    }

    auto sobty_entry = SOB_TYPES.find(sobty);
    if( sobty_entry == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return NULL;
    }

    double tick = 0;
    try{
        tick = sobty_entry->second.second.price_to_tick(price);
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyFloat_FromDouble(tick);
}


PyObject*
ticks_in_range(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::sob_type, Strings::lower,
                              Strings::upper, NULL};

    int sobty;
    double lower;
    double upper;

    if( !MethodArgs::parse(args, kwds, "idd", kwlist, &sobty, &lower, &upper) ){
        return NULL;
    }

    auto sobty_entry = SOB_TYPES.find(sobty);
    if( sobty_entry == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return NULL;
    }

    long long ticks = 0;
    try{
        ticks = sobty_entry->second.second.ticks_in_range(lower, upper);
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyLong_FromLongLong(ticks);
}

/*
PyObject*
tick_memory_required(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = { Strings::sob_type, Strings::lower,
                              Strings::upper, NULL};

    int sobty;
    double lower;
    double upper;

    if( !MethodArgs::parse(args, kwds, "idd", kwlist, &sobty, &lower, &upper) ){
        return NULL;
    }

    auto sobty_entry = SOB_TYPES.find(sobty);
    if( sobty_entry == SOB_TYPES.end() ){
        PyErr_SetString(PyExc_ValueError, "invalid orderbook type");
        return NULL;
    }

    unsigned long long mem = 0;
    try{
        mem = sobty_entry->second.second.tick_memory_required(lower, upper);
    }catch(std::exception& e){
        CONVERT_AND_THROW_NATIVE_EXCEPTION(e);
    }
    return PyLong_FromUnsignedLongLong(mem);
}
*/

PyMethodDef methods[] = {
    MDef::KeyArgs("tick_size", tick_size,
                  "tick size of orderbook \n\n"
                  "def tick_size(sobty) -> tick size \n\n"
                  "    sobty :: int :: SOB_* constant of orderbook type \n\n"
                  "    returns -> float \n"),

    MDef::KeyArgs("price_to_tick", price_to_tick,
                  "convert a price to a tick value \n\n"
                  "    def price_to_tick(sobty, price) -> tick \n\n"
                  "    sobty :: int   :: SOB_* constant of orderbook type \n"
                  "    price :: float :: price \n\n"
                  "    returns -> float \n"),

    MDef::KeyArgs("ticks_in_range", ticks_in_range,
                  "number of ticks between two prices \n\n"
                  "    def ticks_in_range(sobty, lower, upper) "
                  "-> number of ticks \n\n"
                  "    sobty :: int   :: SOB_* constant of orderbook type \n"
                  "    lower :: float :: lower price \n"
                  "    upper :: float :: upper price \n\n"
                  "    returns -> int \n"),
/*
    MDef::KeyArgs("tick_memory_required", tick_memory_required,
                  "bytes of memory required for (pre-allocating) orderbook "
                  "internals. THIS IS NOT TOTAL MEMORY NEEDED! \n\n"
                  "    def tick_memory_required(sobty, lower, upper) "
                  "-> number of bytes \n\n"
                  "    sobty :: int   :: SOB_* constant of orderbook type \n"
                  "    lower :: float :: lower price \n"
                  "    upper :: float :: upper price \n\n"
                  "    returns -> int \n"),
*/
    {NULL}
};


struct PyModuleDef module_def= {
    PyModuleDef_HEAD_INIT,
    "simpleorderbook",
    NULL,
    -1,
    methods,
    0, 0, 0, 0
};


PyObject*
atexit_callee(PyObject *self, PyObject *args)
{
    exiting_pre_finalize = true;
    Py_RETURN_NONE;
}


void
register_atexit_callee()
{
    static PyMethodDef def = {
            "__atexit_callee", atexit_callee, METH_NOARGS, ""
    };
    PyCFunctionObject *func = (PyCFunctionObject *)PyCFunction_New(&def, NULL);
    if( func == NULL ){
        std::cerr << "warn: failed to create atexit_callee function object"
                  << std::endl;
        return;
    }

    PyObject *mod = PyImport_ImportModule("atexit");
    if( !mod ){
        std::cerr << "warn: failed to import 'atexit'" << std::endl;
        Py_DECREF(func);
        return;
    }

    PyObject *ret = PyObject_CallMethod(mod, "register", "O", func);
    Py_DECREF(func);
    Py_DECREF(mod);
    if( ret ){
        Py_DECREF(ret);
    }else{
        std::cerr<< "warn: failed to register atexit_callee" << std::endl;
    }
}


template<typename T>
inline std::string
attr_str(T val)
{ return val; }

template<typename T>
inline std::string
attr_str(const std::pair<std::string, T>& val)
{ return val.first; }

template<typename T>
void
set_const_attributes(PyObject *mod, const std::map<int, T>& m)
{
    for( const auto& p : m ){
        PyObject *indx = Py_BuildValue("i",p.first);
        PyObject_SetAttrString(mod, attr_str(p.second).c_str(), indx);
    }
}

} /* namespace */


volatile bool exiting_pre_finalize = false;

PyMODINIT_FUNC
PyInit_simpleorderbook(void)
{
    if( PyType_Ready(&pySOB_type) < 0
        || PyType_Ready(&pyOrderInfo_type) < 0
        || PyType_Ready(&pyAOT_type) < 0
        || PyType_Ready(&pyAOT_OCO_type) < 0
        || PyType_Ready(&pyAOT_OTO_type) < 0
        || PyType_Ready(&pyAOT_FOK_type) < 0
        || PyType_Ready(&pyAOT_BRACKET_type) < 0
        || PyType_Ready(&pyAOT_TrailingStop_type) < 0
        || PyType_Ready(&pyAOT_TrailingBracket_type) < 0
        || PyType_Ready(&pyAOT_BRACKET_Active_type) < 0
        || PyType_Ready(&pyAOT_TrailingBracket_Active_type) < 0
        || PyType_Ready(&pyAOT_TrailingStop_Active_type) < 0
        || PyType_Ready(&pyAOT_AON_type) < 0 )
    {
        return NULL;
    }

    PyObject *mod = PyModule_Create(&module_def);
    if( !mod ){
        return NULL;
    }

    Py_INCREF(&pySOB_type);
    PyModule_AddObject(mod, "SimpleOrderbook", (PyObject*)&pySOB_type);

    Py_INCREF(&pyOrderInfo_type);
    PyModule_AddObject(mod, "OrderInfo", (PyObject*)&pyOrderInfo_type);

    Py_INCREF(&pyAOT_type);
    PyModule_AddObject(mod, "AdvancedOrderTicket", (PyObject*)&pyAOT_type);

    Py_INCREF(&pyAOT_OCO_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketOCO", (PyObject*)&pyAOT_OCO_type);

    Py_INCREF(&pyAOT_OTO_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketOTO", (PyObject*)&pyAOT_OTO_type);

    Py_INCREF(&pyAOT_FOK_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketFOK", (PyObject*)&pyAOT_FOK_type);

    Py_INCREF(&pyAOT_BRACKET_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketBRACKET",
            (PyObject*)&pyAOT_BRACKET_type);

    Py_INCREF(&pyAOT_TrailingStop_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketTrailingStop",
            (PyObject*)&pyAOT_TrailingStop_type);

    Py_INCREF(&pyAOT_TrailingBracket_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketTrailingBracket",
            (PyObject*)&pyAOT_TrailingBracket_type);

    Py_INCREF(&pyAOT_AON_type);
    PyModule_AddObject(mod, "AdvancedOrderTicketAON", (PyObject*)&pyAOT_AON_type);

    set_const_attributes(mod, SOB_TYPES);
    set_const_attributes(mod, ORDER_TYPES);
    set_const_attributes(mod, CALLBACK_MESSAGES);
    set_const_attributes(mod, SIDES_OF_MARKET);
    set_const_attributes(mod, ORDER_CONDITIONS);
    set_const_attributes(mod, CONDITION_TRIGGERS);

    register_atexit_callee();

    /* build datetime */
    std::string dt = std::string(__DATE__) + " " + std::string(__TIME__);
    PyObject_SetAttrString( mod, "_BUILD_DATETIME",
            Py_BuildValue("s", dt.c_str()) );

    /* is debug build */
    PyObject_SetAttrString( mod, "_BUILD_IS_DEBUG",
            is_debug_build ? Py_True : Py_False );

    PyEval_InitThreads();
    return mod;
}

std::string
to_string(PyObject *arg)
{
    // TODO inspect the PyObject
    std::stringstream ss;
    ss<< std::hex << static_cast<void*>(arg);
    return ss.str();
}
