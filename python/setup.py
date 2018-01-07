# Copyright (C) 2015 Jonathon Ogden     < jeog.dev@gmail.com >

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNE A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see http://www.gnu.org/licenses. 

from distutils.core import setup,Extension

cpp_sources = ["../simpleorderbook.cpp", "simpleorderbook_py.cpp"]

_setup_dict = {
    "name":'simpleorderbook',
    "version":'0.3',
    "description": "financial-market order book",
    "author":"Jonathon Ogden",
    "author_email":"jeog.dev@gmail.com"
}   

_cpp_ext = Extension(
    "simpleorderbook",
    sources = cpp_sources,
    include_dirs = ["../"],
    extra_compile_args=["-fPIC","-std=c++11", "-g", "-O0"]
 )

setup( ext_modules=[_cpp_ext], **_setup_dict )    


    
