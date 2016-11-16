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

py_include_dir = "/usr/include/python3.4m"
py_library_dir = "/usr/lib/python3.4/config-3.4m-x86_64-linux-gnu"
py_library_name = "python3.4m"

cpp_sources = ["simpleorderbook_py.cpp","marketmaker_py.cpp", # py wrapper 
               "../simpleorderbook.cpp", "../marketmaker.cpp"] # native

_setup_dict = {
    "name":'simpleorderbook',
    "version":'0.2',
    "description": "financial-market (vanilla) order matching simulator",
    "author":"Jonathon Ogden",
    "author_email":"jeog.dev@gmail.com"
}   

_cpp_ext = Extension(
    "simpleorderbook",
    sources = cpp_sources,
    include_dirs = [py_include_dir,"../"],
    library_dirs = [py_library_dir],
    libraries = [py_library_name],
    extra_compile_args=["-fPIC","-std=c++11"]
 )

setup( ext_modules=[_cpp_ext], **_setup_dict )    


    
