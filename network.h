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

    void recieve_and_write_file(int socket, const string& file_name);
    vector<Chunk> recieve_file_data(int socket);
    Header get_header(int socket);
    void get_chunk(int socket, Buffer& b, ssize_t len, bool last);

    void send_request(int socket, const string& command);
    string recieve_response(int socket, ssize_t len);
}

#endif