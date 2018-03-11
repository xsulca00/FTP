#ifndef _UTILITY_H
#define _UTILITY_H

#include <sstream>
#include <stdexcept>
#include <string>

namespace utility {
    using namespace std;

    template<typename Target = string, typename Source = string>
    Target to(Source arg)
    {
        stringstream s;
        Target t;

        if (!(s << arg) || !(s >> t) || !(s >> ws).eof()) {
            throw runtime_error {"to<>() failed!"};
        }
        return t;
    }

    void remove_trailing_rn(string& s);
    void check_negative(const string& s);
}


#endif
