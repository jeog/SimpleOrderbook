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

#ifndef JO_FUNCTIONAL_TEST
#define JO_FUNCTIONAL_TEST

#include "../test.hpp"

#ifdef RUN_FUNCTIONAL_TESTS

#include <map>
#include <iostream>


int
run_tick_price_tests(int argc, char* argv[]);

int
run_orderbook_tests(int argc, char* argv[]);

extern const categories_ty functional_categories;

#define DECL_TICK_TEST_FUNC(name) \
int TEST_##name(std::ostream& out)

#define DECL_SOB_TEST_FUNC(name) \
int TEST_##name(sob::FullInterface *orderbook, std::ostream& out)

/* orderbook.cpp */
DECL_TICK_TEST_FUNC(tick_price_1);
DECL_SOB_TEST_FUNC(grow_1);
DECL_SOB_TEST_FUNC(grow_2);
DECL_SOB_TEST_FUNC(grow_ASYNC_1);
/* basic_orders.cpp */
DECL_SOB_TEST_FUNC(basic_orders_1);
DECL_SOB_TEST_FUNC(basic_orders_2);
DECL_SOB_TEST_FUNC(stop_orders_1);
DECL_SOB_TEST_FUNC(basic_orders_ASYNC_1);
/* pull_replace.cpp */
DECL_SOB_TEST_FUNC(orders_info_pull_1);
DECL_SOB_TEST_FUNC(orders_info_pull_ASYNC_1);
DECL_SOB_TEST_FUNC(replace_order_1);
DECL_SOB_TEST_FUNC(replace_order_ASYNC_1);
/* advanced_orders/once_cancels_other.cpp */
DECL_SOB_TEST_FUNC(advanced_OCO_1);
DECL_SOB_TEST_FUNC(advanced_OCO_2);
DECL_SOB_TEST_FUNC(advanced_OCO_3);
DECL_SOB_TEST_FUNC(advanced_OCO_4);
DECL_SOB_TEST_FUNC(advanced_OCO_5);
DECL_SOB_TEST_FUNC(advanced_OCO_ASYNC_1);
DECL_SOB_TEST_FUNC(advanced_OCO_ASYNC_2);
/* advanced_orders/bracket.cpp */
DECL_SOB_TEST_FUNC(advanced_BRACKET_1);
DECL_SOB_TEST_FUNC(advanced_BRACKET_2);
DECL_SOB_TEST_FUNC(advanced_BRACKET_3);
DECL_SOB_TEST_FUNC(advanced_BRACKET_4);
DECL_SOB_TEST_FUNC(advanced_BRACKET_5);
DECL_SOB_TEST_FUNC(advanced_BRACKET_6);
DECL_SOB_TEST_FUNC(advanced_BRACKET_7);
DECL_SOB_TEST_FUNC(advanced_BRACKET_8);
/* advanced_orders/one_triggers_other.cpp */
DECL_SOB_TEST_FUNC(advanced_OTO_1);
DECL_SOB_TEST_FUNC(advanced_OTO_2);
DECL_SOB_TEST_FUNC(advanced_OTO_3);
/* advanced_orders/fill_or_kill.cpp */
DECL_SOB_TEST_FUNC(advanced_FOK_1);
/* advanced_orders/trailing_stop.cpp */
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_1);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_2);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_3);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_4);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_5);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_6);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_7);
/* advanced_orders/trailing_bracket.cpp */
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_1);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_2);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_3);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_4);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_5);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_6);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_7);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_8);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_9);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_10);
/* advanced_orders/all_or_none.cpp */
DECL_SOB_TEST_FUNC(advanced_AON_1);
DECL_SOB_TEST_FUNC(advanced_AON_2);
DECL_SOB_TEST_FUNC(advanced_AON_3);
DECL_SOB_TEST_FUNC(advanced_AON_4);
DECL_SOB_TEST_FUNC(advanced_AON_5);
DECL_SOB_TEST_FUNC(advanced_AON_6);
DECL_SOB_TEST_FUNC(advanced_AON_7);
DECL_SOB_TEST_FUNC(advanced_AON_8);
DECL_SOB_TEST_FUNC(advanced_AON_9);
DECL_SOB_TEST_FUNC(advanced_AON_10);
DECL_SOB_TEST_FUNC(advanced_AON_11);
DECL_SOB_TEST_FUNC(advanced_AON_12);
DECL_SOB_TEST_FUNC(advanced_AON_13);
DECL_SOB_TEST_FUNC(advanced_AON_ASYNC_1);

void
callback( sob::callback_msg msg,
          sob::id_type id1,
          sob::id_type id2,
          double price,
          size_t size);

void
callback_admin( sob::id_type id );

void
print_orderbook_state( sob::FullInterface *ob,
                       std::ostream& out = std::cout,
                       size_t max_depth=8,
                       size_t max_timesales=8,
                       bool dump=false );

void
dump_orders(sob::FullInterface *orderbook, std::ostream& out = std::cout);

sob::order_exec_cb_type
create_advanced_callback(std::map<sob::id_type, sob::id_type>& ids);

#endif /* RUN_FUNCTIONAL_TESTS */

#endif /* JO_FUNCTIONAL_TEST */
