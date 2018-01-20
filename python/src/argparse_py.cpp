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

#ifndef IGNORE_TO_DEBUG_NATIVE

char MethodArgs::id[] = "id";
char MethodArgs::stop[] = "stop";
char MethodArgs::limit[] = "limit";
char MethodArgs::size[] = "size";
char MethodArgs::callback[] = "callback";
char MethodArgs::depth[] = "depth";
char MethodArgs::sob_type[] = "sob_type";
char MethodArgs::low[] = "low";
char MethodArgs::high[] = "high";
char MethodArgs::new_max[] = "new_max";
char MethodArgs::new_min[] = "new_min";
char MethodArgs::price[] = "price";
char MethodArgs::lower[] = "lower";
char MethodArgs::upper[] = "upper";

const std::map<sob::order_type, std::array<char*,6>>
OrderMethodArgsBase::keywords = {
    {sob::order_type::limit, {id, limit, size, callback}},
    {sob::order_type::market, {id, size, callback}},
    {sob::order_type::stop, {id, stop, size, callback}},
    {sob::order_type::stop_limit, {id, stop, limit, size, callback}}
};

const std::map<sob::order_type, std::string>
OrderMethodArgsBase::format_strs = {
    {sob::order_type::limit, "kdl|O"},
    {sob::order_type::market, "kl|O"},
    {sob::order_type::stop, "kdl|O"},
    {sob::order_type::stop_limit, "kddl|O"}
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

#endif /* IGNORE_TO_DEBUG_NATIVE */
