#ifndef NETWORK_H
#define NETWORK_H

extern "C" {
#include <unistd.h>
#include <netdb.h>
}

#include "file.h"
#include <string>
#include <stdexcept>

namespace network {
    using namespace std;
    using namespace file;

    struct Header {
        Header() : length{}, last{} {}
        Header(int llength, bool llast) : length{llength}, last{llast} {}

        int length;
        bool last;
    };

    class Socket {
    public:
        Socket(int s) : socket{s} { if (socket <= 0) throw runtime_error{"Invalid socket!"}; }
        ~Socket() { close(socket); }

        operator int() { return socket; }
    private:
        int socket;
    };

    hostent* get_host(const string& name);
    sockaddr_in server_address(const hostent* host, uint16_t port);
    void connect_to_server(int socket, const string& host_name, uint16_t port);
    int create_socket();

    void recieve_and_write_file(int socket, ofstream& file);
    vector<Chunk> recieve_file_data(int socket);
    Header get_header(int socket);
    void get_chunk(int socket, Buffer& b, ssize_t len, bool last);
    void send_bytes(int socket, const char* data, ssize_t len);

    void send_response(int socket, const string& command);
    void send_request(int socket, const string& command);
    string recieve_response(int socket, ssize_t len);
    string recieve_request(int socket, ssize_t len);
}

#endif
