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
#ifndef SOB_TEST_TEST
#define SOB_TEST_TEST

#ifdef DEBUG
#undef NDEBUG
#else
#define NDEBUG
#endif

/*
 * DEFAULT BEHAVIOR:
 *
 * DEBUG build: run functional tests
 * RELEASE build: run performance tests
 */
#ifndef NDEBUG
#define RUN_FUNCTIONAL_TESTS
#else
#define RUN_PERFORMANCE_TESTS
#endif

//#define RUN_ALL_TESTS /* DEBUG */

/* override default behavior */
#ifdef RUN_ALL_TESTS
#define RUN_FUNCTIONAL_TESTS
#define RUN_PERFORMANCE_TESTS
#endif

/* override default behavior (TAKES PRIORITY)*/
#ifdef RUN_NO_TESTS
#undef RUN_FUNCTIONAL_TESTS
#undef RUN_PERFORMANCE_TESTS
#endif

#if !defined(RUN_FUNCTIONAL_TESTS) && !defined(RUN_PERFORMANCE_TESTS)
#define RUN_NO_TESTS
#endif

#ifndef RUN_NO_TESTS
#include <vector>
#include <string>
#include <functional>
#include <ratio>
#include <tuple>

#include "../include/simpleorderbook.hpp"

typedef std::vector<std::pair<std::string, std::function<int(void)>>> categories_ty;

typedef std::tuple<double, double> proxy_args_ty;
typedef std::tuple<int,
              const sob::DefaultFactoryProxy,
              const std::vector<proxy_args_ty>> proxy_info_ty;

template<size_t denom>
constexpr proxy_info_ty
make_proxy_info(const std::vector<proxy_args_ty>& args)
{
    return make_tuple(
            denom,
            sob::SimpleOrderbook::BuildFactoryProxy<std::ratio<1,denom>>(),
            args
            );
}
#endif /* RUN_FUNCTIONAL_TESTS || RUN_PERFORMANCE_TESTS */

#endif /* SOB_TEST_TEST */
