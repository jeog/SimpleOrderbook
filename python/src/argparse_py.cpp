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

#include "../include/argparse_py.hpp"

const std::map<sob::order_type, std::array<char*,7>>
OrderMethodArgsBase::keywords = {
    {sob::order_type::limit, {Strings::id, Strings::limit, Strings::size,
                              Strings::callback, Strings::advanced, NULL, NULL}},
    {sob::order_type::market, {Strings::id, Strings::size, Strings::callback,
                               Strings::advanced, NULL, NULL, NULL}},
    {sob::order_type::stop, {Strings::id, Strings::stop, Strings::size,
                             Strings::callback, Strings::advanced, NULL, NULL}},
    {sob::order_type::stop_limit, {Strings::id, Strings::stop, Strings::limit,
                                   Strings::size, Strings::callback,
                                   Strings::advanced, NULL}}
};

const std::map<sob::order_type, std::string>
OrderMethodArgsBase::format_strs = {
    {sob::order_type::limit, "kdl|OO"},
    {sob::order_type::market, "kl|OO"},
    {sob::order_type::stop, "kdl|OO"},
    {sob::order_type::stop_limit, "kddl|OO"}
};

std::string
MethodArgs::build_keywords_str(char **kwds){
    std::string s;
    for(int i = 0; kwds[i] != NULL; ++i){
        s += kwds[i];
        s += " ";
    }
    if( !s.empty() ){
        s.pop_back();
    }
    return s;
}

