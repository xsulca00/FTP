extern "C" {
#include <unistd.h>
#include <sys/stat.h>
}

#include "utility.h"
#include <string>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace utility {
    void remove_trailing_rn(string& s) {
        auto pos = s.find_last_of('\r');
        if (pos != string::npos) s.erase(pos, pos+1);
        pos = s.find_last_of('\n');
        if (pos != string::npos) s.erase(pos, pos+1);
    }

    void check_negative(const string& s) { if (!s.empty() && s.front() == '-') throw runtime_error{"Port number is negative!"}; }

    pair<vector<string>, string> get_path_and_file(const string& path) {
        vector<string> dirs;

        for (size_t before {0}, found {0}; found != string::npos;) {
            found = path.find_first_of('/', before);
            dirs.push_back(path.substr(before, found-before));
            before = found + 1;
        }

        string file;
        if (!dirs.empty()) {
            file = move(dirs.back());
            dirs.pop_back();
        }

        return {dirs, file};
    }

    bool file_exists(const string& fname) { return access(fname.c_str(), F_OK | W_OK) != -1; }

    bool is_dir(const string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == -1) return false;
        return S_ISDIR(st.st_mode);
    }

    bool is_symlink(const string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == -1) return false;
        return S_ISLNK(st.st_mode);
    }

}
