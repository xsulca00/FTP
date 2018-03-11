extern "C" {
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
}

#include <cstring>
#include <fstream>
#include <iostream>
#include <array>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <tuple>
#include <map>
#include "../utility.h"
#include "../network.h"
#include "../file.h"

using namespace std;
using namespace utility;
using namespace network;
using namespace network::server;
using namespace file;

enum class Flags { none, read, write };

struct Args {
    string port {};
}; 

map<int, string> status_messages {
    {100, "100 Begin file transfer\r\n"},
    {101, "101 Can not open a file\r\n"},
    {102, "102 File transfer completed\r\n"}
};

void check_flag(Flags f) {
    if (f != Flags::none) throw runtime_error{"check_flag(): read or write is already set!"};
}

Args args(int argc, char* argv[]) {
    Args a;

    for(int c {}; (c = getopt(argc, argv, "p:")) != -1;) {
        switch (c) {
            case 'p': a.port = optarg; check_negative(a.port); break;
            case '?': throw runtime_error{""};
        }
    }

    return a;
}

void check_arguments(const Args& a) {
    if (a.port.empty()) throw runtime_error{"main(): port not set!"};
}

string make_command(Flags f, const string& file) {
    string command;
    switch (f) {
        case Flags::read: command += "READ" ; break;
        case Flags::write: command += "WRITE"; break;
        default: throw runtime_error{"invalid flag set, not flags::read nor flags::write"};
    }
    return command += ' ' + file + "\r\n";
}

void send_bytes(int socket, const char* data, ssize_t len) {
    ssize_t bytes {send(socket, data, len, 0)};
    if (bytes < 0) throw system_error{errno, generic_category()};
}

string recieve_request(int socket, ssize_t len) {
    cout << "Recieving request\n";
    string s(len, '\0');

    ssize_t bytes {};
    char* b {&s[0]};
    cout << "Before loop\n";
    for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, b, remainder, 0);
        if (!bytes) {
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

Flags get_request_type(const string& s) {
    auto pos = s.find("READ ");
    if (pos != string::npos) {
        // discard newline and carriage return
        // string file {s.substr(pos+sizeof("READ ")-1, s.length()-2)};
        return Flags::read;
    }
    pos = s.find("WRITE ");
    if (pos != string::npos) {
        // discard newline and carriage return
        // string file {s.substr(pos+sizeof("WRITE ")-1, s.length()-2)};
        return Flags::write;
    }
    return Flags::none;
}

string get_file_name(int socket, const string& s) {
    auto found {s.find("\r\n")};
    if (found != string::npos) {
        // file name found
        return {s.begin(), s.begin()+found};
    }
    return {};
}

void read_and_send_file(int socket, const string& fname) {
    cout << "Opening file " << fname << '\n';
    ifstream file {fname, ios_base::binary};
    if (!file) {
        cerr << "Cannot open file \"" << fname << "\": " << error_code{errno, generic_category()}.message() << '\n';
        send_response(socket, status_messages[101]);
        return;
    } 

    send_response(socket, status_messages[100]);
    cout << "Sending bytes\n";
    for (Buffer b; file;) {
        file.read(b.data(), b.size());
        Header h {static_cast<int>(file.gcount()), file.eof()};
        cout << "h.length = " << h.length << " h.last = " << h.last << '\n';
        cout << "Data: " << b.data() << '\n';
        send_bytes(socket, as_bytes(h), sizeof(h));
        send_bytes(socket, b.data(), h.length);
    }
    send_response(socket, status_messages[102]);
    cout << "Sending finished\n";
}

void fill_in_buffer(int socket, Buffer& b) {
    ssize_t bytes {};
    for (ssize_t remainder {b.size()}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, b.data(), remainder, 0);
        if (bytes == 0) throw runtime_error{"Client disconnected"};
        if (bytes < 0) throw system_error{errno, generic_category()};

        {
            // find if there is READ or WRITE operation
            string buffer {b.begin(), b.begin() + bytes};
            Flags f {get_request_type(buffer)};

            string file_name;
            switch (f) {
                case Flags::read: file_name = buffer.substr(sizeof("READ ")-1); break;
                case Flags::write: file_name = buffer.substr(sizeof("WRITE ")-1); break;
                case Flags::none: throw runtime_error{"Expected READ or WRITE operation"};
            }

            // get file name, if not in buffer, recieve more bytes
            string fname {get_file_name(socket, file_name)};

            cout << "Name: " << fname.back() << '\n';

            /*
            string data {file_name.begin() + fname.size(), file_name.end()};
            cout << "Data: " << data << '\n';
            */
            if (!fname.empty()) {
                if (f == Flags::read) {
                    cout << "READ accepted\n";
                    read_and_send_file(socket, fname);
                    return;
                } else if (f == Flags::write) {
                }
            }
        }
    }
}

int main(int argc, char* argv[]) 
try {
    Args a {args(argc, argv)};
    check_arguments(a);

    Socket server {create_socket()};

    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(to<uint16_t>(a.port));

    int rc {};
    if ((rc = bind(server, (sockaddr*)&sa, sizeof(sa))) < 0) {
        throw runtime_error{"Bind failed"};
    }

    if (listen(server, 1) < 0) {
        throw runtime_error{"Listen failed"};
    }

    const string begin_file_transfer {"100 Begin file transfer\r\n"};
    const string cannot_open_file {"101 Cannot open file\r\n"};
    const string file_transfer_completed {"97 File transfer completed\r\n"};

    for (;;) {
        sockaddr_in client {};
        socklen_t client_len {sizeof(client)};

        try {
            cout << "Waiting for client...\n"; 
            Socket client_socket {accept(server, (sockaddr*)&client, &client_len)};
            cout << "Client accepted\n";

            Buffer b;
            fill_in_buffer(client_socket, b);
        } catch (const runtime_error& e) {
            cerr << "Cannot open communication with client: " << e.what() << '\n';;
        }
    }

    //string command {make_command(a.flags, a.file)};
} catch (const system_error& e) {
    string s {e.what()};
    auto pos = s.find_last_of('\r');
    if (pos != string::npos) s.insert(pos, "\\r");
    pos = s.find_last_of('\n');
    if (pos != string::npos) s.insert(pos, "\\n");

    cerr << "Exception has been thrown: " << s << '\n';
} catch (const exception& e) {
    string s {e.what()};
    auto pos = s.find_last_of('\r');
    if (pos != string::npos) s.insert(pos, "\\r");
    pos = s.find_last_of('\n');
    if (pos != string::npos) s.insert(pos, "\\n");

    cerr << "Exception has been thrown: " << s << '\n';
    return 1;
}
