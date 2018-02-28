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

#ifndef JO_SOB_TEST
#define JO_SOB_TEST

#ifdef DEBUG
#undef NDEBUG
#else
#define NDEBUG
#endif

#ifdef NDEBUG
#define RUN_PERFORMANCE_TESTS
#endif

#ifdef RUN_PERFORMANCE_TESTS

#include "../include/simpleorderbook.hpp"

#define DECL_PERFORMANCE_TEST_FUNC(name) \
double TEST_##name(sob::FullInterface *full_orderbook, int n)

/* tests/insert.cpp */
DECL_PERFORMANCE_TEST_FUNC(n_limits);
DECL_PERFORMANCE_TEST_FUNC(n_basics);
/* tests/pull.cpp */
DECL_PERFORMANCE_TEST_FUNC(n_pulls);
DECL_PERFORMANCE_TEST_FUNC(n_replaces);

std::vector<double>
generate_prices(const sob::FullInterface *ob, double min, double max, int n);

std::vector<size_t>
generate_sizes(size_t min, size_t max, int n);

std::vector<bool>
generate_buy_sells(int n);

std::vector<sob::order_type>
generate_limit_market_stop(int n, int limit_ratio=1);

std::vector<sob::order_type>
generate_limit_stop(int n, int limit_ratio=1);

#endif /* RUN_PERFORMANCE_TESTS */

#endif /* JO_SOB_TEST */
