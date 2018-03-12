#ifndef FILE_H
#define FILE_H

#include <string>
#include <vector>
#include <fstream>
#include <array>

namespace file {
    using namespace std;

    using Buffer = array<char, 4096>;

    struct Chunk {
        Chunk() : data{{}}, size{} {}

        Buffer data;
        long size;
    };

    void write_data_to_file(ofstream& file, const vector<Chunk>& data);
}

#endif
