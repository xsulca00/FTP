extern "C" {
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/stat.h>
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
using namespace file;

enum class Flags { none, read, write };

struct Args {
    string port {};
}; 

map<int, string> status_messages {
    {100, "100 Begin file transfer\r\n"},
    {101, "101 Can not open a file\r\n"},
    {102, "102 Transfer completed.\r\n"}
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

string get_file_name(const string& s) {
    auto found {s.find("\r\n")};
    if (found != string::npos) {
        // file name found
        return {s.begin(), s.begin()+found};
    }
    return {};
}

void open_and_recieve_file(int socket, const string& fname) {

    cout << "Checking file path\n";
    if (!fname.empty() && fname.front() == '/') {
        cerr << "Cannot access root directory: " << fname << '\n';
        send_response(socket, status_messages[101]);
        return;
    }

    auto p = get_path_and_file(fname);

    if (!file_exists(fname)) {
        string path;
        for (const string& dir : p.first) {
            path += dir + '/';
            if (!file_exists(path) && mkdir(path.c_str(), 0777) < 0) {
                perror("mkdir");
                send_response(socket, status_messages[101]);
                return;
            }
        }
    }

    ofstream file {fname, ios_base::binary};
    if (!file) {
        cerr << "Cannot open file \"" << fname << "\": " << error_code{errno, generic_category()}.message() << '\n';
        send_response(socket, status_messages[101]);
        return;
    } 
    cout << "File path checked\n";

    send_response(socket, status_messages[100]);
    cout << "Recieving bytes\n";
    recieve_and_write_file(socket, file);
    cout << "Recieving finished\n";
    send_response(socket, status_messages[102]);
}

void open_and_send_file(int socket, const string& fname) {
    cout << "Opening file " << fname << '\n';

    if (!fname.empty() && fname.front() == '/') {
        cerr << "Cannot access root directory: " << fname << '\n';
        send_response(socket, status_messages[101]);
        return;
    }

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

            string fname {get_file_name(file_name)};

            if (!fname.empty()) {
                if (f == Flags::read) {
                    cout << "READ accepted\n";
                    open_and_send_file(socket, fname);
                    return;
                } else if (f == Flags::write) {
                    cout << "WRITE accepted\n";
                    open_and_recieve_file(socket, fname);
                    return;
                }
            }
        }
    }
}

int socket_to_close;

void cleanup(int, siginfo_t*, void*) {
    cerr << "SIGINT registerered!\n";
    close(socket_to_close);
    exit(1);
}

int main(int argc, char* argv[]) 
try {
    Args a {args(argc, argv)};
    check_arguments(a);

    Socket server {create_socket()};
    socket_to_close = server;

    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(to<uint16_t>(a.port));

    int rc {};
    if ((rc = bind(server, (sockaddr*)&sa, sizeof(sa))) < 0) { throw runtime_error{"Bind failed"}; }
    if (listen(server, 1) < 0) { throw runtime_error{"Listen failed"}; }

    struct sigaction act {};
    act.sa_sigaction = &cleanup;
    act.sa_flags = SA_SIGINFO;

    if (sigaction(SIGINT, &act, nullptr) < 0) throw system_error{errno, generic_category()};

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
            cerr << "Exception has been thrown: " << e.what() << '\n';;
        }
    }

    //string command {make_command(a.flags, a.file)};
} catch (const system_error& e) {
    string s {e.what()};
    remove_trailing_rn(s);
    cerr << "Exception has been thrown: " << s << '\n';
    return 1;
} catch (const exception& e) {
    string s {e.what()};
    remove_trailing_rn(s);
    cerr << "Exception has been thrown: " << s << '\n';
    return 1;
}


