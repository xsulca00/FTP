#include "network.h"

extern "C" {
#include <netdb.h>
#include <sys/socket.h>
}

#include <string>
#include <stdexcept>
#include <algorithm>
#include <system_error>
#include <iostream>
#include <fstream>

namespace network {
    hostent* get_host(const string& name) {
        hostent* host {gethostbyname(name.c_str())};
        if (!host) throw runtime_error{"Host with name " + name + " not found!"};
        return host;
    }

    sockaddr_in server_address(const hostent* host, uint16_t port) {
        sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        copy_n((char*)host->h_addr, host->h_length, (char*)&server.sin_addr.s_addr);
        return server;
    }

    void connect_to_server(int socket, const string& host_name, uint16_t port) {
        hostent* host {get_host(host_name)};
        sockaddr_in server = server_address(host, port);
        if (connect(socket, (sockaddr*)&server, sizeof(server))) throw system_error{errno, generic_category()};
    }

    int create_socket() { return socket(AF_INET, SOCK_STREAM, 0); }

    Header get_header(int socket) {
        ssize_t bytes {};
        Header h;
        for (ssize_t remainder {sizeof(h)}; remainder > 0; remainder -= bytes) {
            bytes = recv(socket, &h, remainder, 0);
            if (bytes == 0) throw system_error{errno, generic_category()};
            if (bytes < 0) throw system_error{errno, generic_category()};
        }
        return h;
    }

    void get_chunk(int socket, Buffer& b, ssize_t len, bool last) {
        ssize_t bytes {};
        cout << "get_chunk(): len: " << len << '\n';
        for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
            cout << "get_chunk(): before recv\n";
            bytes = recv(socket, b.data(), remainder, 0);
            cout << "get_chunk(): after recv\n";
            cout << "get_chunk(): bytes: " << bytes << '\n';
            if (bytes == 0) throw system_error{errno, generic_category()};
            if (bytes < 0) throw system_error{errno, generic_category()};
            if (last) return;
        }
    }

    vector<Chunk> recieve_file_data(int socket) {
        vector<Chunk> chunks;
        Header h;
        Chunk c;
        cout << "Recieving file\n";
        while (!h.last) {
            cout << "Getting header\n";
            h = get_header(socket);
            if (h.length < 0) throw runtime_error{"Header length=(" + to_string(h.length) + ") <= 0"};

            cout << "h.length = " << h.length << " h.last = " << h.last << '\n';

            chunks.push_back(Chunk{});
            cout << "Getting chunk\n";
            get_chunk(socket, chunks.back().data, h.length, h.last);
            chunks.back().size = h.length;
        }
        cout << "Chunks return\n";
        return chunks;
    }

    void recieve_and_write_file(int socket, ofstream& file) {
        vector<Chunk> file_data {recieve_file_data(socket)};
        write_data_to_file(file, file_data);
    }

    void send_bytes(int socket, const char* data, ssize_t len) {
        if (len <= 0) return;
        ssize_t bytes {send(socket, data, len, 0)};
        if (bytes < 0) throw system_error{errno, generic_category()};
        if (bytes == 0) throw system_error{errno, generic_category()};
    }

    void send_request(int socket, const string& command) { send_bytes(socket, command.c_str(), command.length()); }
    void send_response(int socket, const string& command) { send_bytes(socket, command.c_str(), command.length()); }

    string recieve_request(int socket, ssize_t len) {
        cout << "Recieving request\n";
        string s(len, '\0');

        ssize_t bytes {};
        char* b {&s[0]};
        cout << "Before loop\n";
        for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
            bytes = recv(socket, b, remainder, 0);
            if (bytes == 0) {
                cout << "Client disconnected\n";
                break;
            }
            cout << "remainder: " << remainder << " bytes: " << bytes << '\n';
            if (bytes < 0) throw system_error{errno, generic_category()};
            b += bytes;

            if (s.find("READ ") != string::npos || s.find("WRITE ") != string::npos) break;
        }
        cout << "After loop\n";

        return s;
    }

    string recieve_response(int socket, ssize_t len) {
        string s(len, '\0');

        ssize_t bytes {};
        char* b {&s[0]};
        for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
            bytes = recv(socket, b, remainder, 0);
            if (bytes == 0) throw system_error{errno, generic_category()};
            if (bytes < 0) throw system_error{errno, generic_category()};
            b += bytes;
        }

        return s;
    }

}

