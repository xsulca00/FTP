#include "file.h"

#include <string>
#include <vector>
#include <stdexcept>

namespace file {
    void write_data_to_file(const string& name, const vector<Chunk>& data) {
        ofstream file {name, ios_base::binary};
        if (!file) throw runtime_error{"Cannot open file: \"" + name + '"'};
        for (const Chunk& c : data) file.write(c.data.data(), c.size);
    }
}
