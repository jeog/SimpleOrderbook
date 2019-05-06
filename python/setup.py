#
# Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNE A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see http://www.gnu.org/licenses. 
#

from distutils.core import setup, Extension
from os import walk as _walk
from os.path import join as _join, dirname as _dirname
from glob import glob as _glob

NAME = 'simpleorderbook'
SOURCE_DIR = _join(_dirname(__file__),'src')

setup_dict = {
    "name":NAME,
    "version":'0.6',
    "description": "financial-market orderbook and matching engine",
    "author":"Jonathon Ogden",
    "author_email":"jeog.dev@gmail.com"
} 

cpp_sources = [f for d,_,files in _walk(SOURCE_DIR) \
               for f in _glob(_join(d, "*.cpp")) + _glob(_join(d, "*.c"))]

cpp_include_dirs = ["../include", "./include"]
cpp_compile_flags = ["-std=c++11", "-Wno-invalid-offsetof"]

cpp_ext = Extension(
    NAME,
    sources = cpp_sources,
    include_dirs = cpp_include_dirs,
    libraries=['SimpleOrderbook'],
    library_dirs=["../bin/release"],
    extra_compile_args=cpp_compile_flags,    
    undef_macros=["NDEBUG"], #internal DEBUG/NDEBUG checks should handle this
)

if __name__ == '__main__':
    setup( ext_modules=[cpp_ext], **setup_dict )    

   
