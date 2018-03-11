#include "file.h"

#include <string>
#include <vector>
#include <stdexcept>

namespace file {
    void write_data_to_file(ofstream& file, const vector<Chunk>& data) {
        if (file) {
            for (const Chunk& c : data) file.write(c.data.data(), c.size);
        }
    }
}
