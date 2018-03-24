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

#ifndef JO_SOB_STRINGS_PY
#define JO_SOB_STRINGS_PY

/*
 * define certain C strings to avoid the deprecated string literal -> char*
 * casts required by parts of the Python-C API
 *
 * IMPORTANT: BUILD_PY_OBJ_MEMBER_DEF macro requires the pyAOT member name
 *            match the variable name below AND have a _doc version
 *
 */

struct Strings{
    static char
    /* method args and pyAOT/pyOrderInfo member names */
    id[],
    stop[],
    limit[],
    size[],
    callback[],
    depth[],
    sob_type[],
    low[],
    high[],
    new_max[],
    new_min[],
    price[],
    lower[],
    upper[],
    is_buy[],
    condition[],
    trigger[],
    loss_stop[],
    loss_limit[],
    target_limit[],
    nticks[],
    stop_nticks[],
    target_nticks[],
    advanced[],
    order_type[],
    /* pyAOT/pyOrderInfo member doc strings */
    condition_doc[],
    trigger_doc[],
    is_buy_doc[],
    size_doc[],
    limit_doc[],
    stop_doc[],
    loss_limit_doc[],
    loss_stop_doc[],
    target_limit_doc[],
    nticks_doc[],
    stop_nticks_doc[],
    target_nticks_doc[],
    order_type_doc[],
    advanced_doc[];
};

#endif /* JO_SOB_STRINGS_PY */
