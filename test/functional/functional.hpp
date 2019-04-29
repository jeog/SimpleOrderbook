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
int TEST_##name()

#define DECL_SOB_TEST_FUNC(name) \
int TEST_##name(sob::FullInterface *orderbook)

/* orderbook.cpp */
DECL_TICK_TEST_FUNC(tick_price_1);
DECL_SOB_TEST_FUNC(grow_1);
DECL_SOB_TEST_FUNC(grow_2);
/* basic_orders.cpp */
DECL_SOB_TEST_FUNC(basic_orders_1);
DECL_SOB_TEST_FUNC(basic_orders_2);
/* pull_replace.cpp */
DECL_SOB_TEST_FUNC(orders_info_pull_1);
DECL_SOB_TEST_FUNC(replace_order_1);
/* advanced_oco.cpp */
DECL_SOB_TEST_FUNC(advanced_OCO_1);
DECL_SOB_TEST_FUNC(advanced_OCO_2);
DECL_SOB_TEST_FUNC(advanced_OCO_3);
DECL_SOB_TEST_FUNC(advanced_OCO_4);
/* advanced_bracket.cpp */
DECL_SOB_TEST_FUNC(advanced_BRACKET_1);
DECL_SOB_TEST_FUNC(advanced_BRACKET_2);
DECL_SOB_TEST_FUNC(advanced_BRACKET_3);
/* advanced_oto.cpp */
DECL_SOB_TEST_FUNC(advanced_OTO_1);
DECL_SOB_TEST_FUNC(advanced_OTO_2);
DECL_SOB_TEST_FUNC(advanced_OTO_3);
/* advanced_fok.cpp */
DECL_SOB_TEST_FUNC(advanced_FOK_1);
/* advanced_trailing_stop.cpp */
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_1);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_2);
DECL_SOB_TEST_FUNC(advanced_TRAILING_STOP_3);
/* advanced_trailing_bracket.cpp */
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_1);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_2);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_3);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_4);
DECL_SOB_TEST_FUNC(advanced_TRAILING_BRACKET_5);

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
                       size_t max_depth=8,
                       size_t max_timesales=8,
                       bool dump=false );

void
dump_orders(sob::FullInterface *orderbook, std::ostream& out = std::cout);

sob::order_exec_cb_type
create_advanced_callback(std::map<sob::id_type, sob::id_type>& ids);

#endif /* RUN_FUNCTIONAL_TESTS */

#endif /* JO_FUNCTIONAL_TEST */
