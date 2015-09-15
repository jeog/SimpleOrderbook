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

py_include_dir = "/usr/local/include/python3.4m"
py_library_dir = "/usr/lib/python3.2/config-3.2mu"
py_library_name = "python3.2mu"

_setup_dict = {
  "name":'simpleorderbook',
  "version":'0.1',
  "description": "Interface to an interactive back-end"
                 " for handling financial market order flow.",
  "author":"Jonathon Ogden",
  "author_email":"jeog.dev@gmail.com"
}   

_cpp_ext = Extension(
  "simpleorderbook",
  sources=["simpleorderbook.cpp","marketmaker.cpp",
          "../simple_orderbook.cpp","../market_maker.cpp"],
  include_dirs=[py_include_dir,"../"],
  library_dirs=[py_library_dir],
  libraries=[py_library_name],
  extra_compile_args=["-fPIC","-std=c++0x"] 
  )

setup( ext_modules=[_cpp_ext], **_setup_dict )    


    
