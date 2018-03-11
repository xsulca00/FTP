#include "utility.h"
#include <string>
#include <stdexcept>

namespace utility {
    void remove_trailing_rn(string& s) {
        auto pos = s.find_last_of('\r');
        if (pos != string::npos) s.erase(pos, pos+1);
        pos = s.find_last_of('\n');
        if (pos != string::npos) s.erase(pos, pos+1);
    }

    void check_negative(const string& s) { if (!s.empty() && s.front() == '-') throw runtime_error{"Port number is negative!"}; }

}
