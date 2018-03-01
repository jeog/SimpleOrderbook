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

#include "functional/functional.hpp"
#include "performance/performance.hpp"

namespace{

#if defined(RUN_FUNCTIONAL_TESTS) || defined(RUN_PERFORMANCE_TESTS)
int
run(std::string name, const categories_ty& categories)
{
    using namespace std;
    cout<< "*** BEGIN SIMPLEORDERBOOK " << name << " TESTS ***" <<  endl;
    for( auto& tc : categories ){
        int err = tc.second();
        if( err ){
            cout<< endl << endl
                << "*** " << tc.first << " ERROR (" << err << ") ***" << endl
                << "*** " << tc.first << " ERROR (" << err << ") ***" << endl
                << "*** " << tc.first << " ERROR (" << err << ") ***" << endl
                << endl << endl;
            return err;
        }
    }
    cout<< "*** END SIMPLEORDERBOOK " << name << " TESTS ***" <<  endl;
    return 0;
}
#endif

}; /* namespace */


int main(int argc, char* argv[])
{
    using namespace std;
    int err = 0;

#ifdef RUN_FUNCTIONAL_TESTS
    err = run("FUNCTIONAL", functional_categories);
    if( err ){
        return err;
    }
#endif /* RUN_FUNCTIONAL_TESTS */

#ifdef RUN_PERFORMANCE_TESTS
    err = run("PERFORMANCE", performance_categories);
    if( err ){
        return err;
    }
#endif /* RUN_PERFORMANCE_TESTS */

#ifdef RUN_NO_TESTS
    cout<< "*** NOT RUNNING ANY TESTS ***" << endl;
#endif

    return err;
}



