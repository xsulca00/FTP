extern "C" {
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
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
#include <map>
#include "network.h"
#include "utility.h"
#include "file.h"

using namespace std;
using namespace network;
using namespace utility;
using namespace file;

enum class Flags { none, read, write };

struct Args {
    string host {};
    string port {};
    Flags flags {Flags::none};
    string file;
}; 

map<int, string> status_messages {
    {100, "100 Begin file transfer\r\n"},
    {101, "101 Can not open a file\r\n"},
    {102, "102 Transfer completed.\r\n"}
};

void check_flag(Flags f);
Args args(int argc, char* argv[]);
void check_arguments(const Args& a);
string make_command(Flags f, const string& file);
void recieve_transfer_completed_message(int socket);
void read_file_from_server(int socket, const string& fname);

void send_file(int socket, ifstream& file) {
    cout << "Sending bytes\n";
    for (Buffer b; file;) {
        file.read(b.data(), b.size());
        Header h {static_cast<int>(file.gcount()), file.eof()};
        cout << "h.length = " << h.length << " h.last = " << h.last << '\n';
        cout << "Data: " << b.data() << '\n';
        send_bytes(socket, as_bytes(h), sizeof(h));
        send_bytes(socket, b.data(), h.length);
    }
    cout << "Sending finished\n";
}

void write_file_to_server(int socket, const string& fname) {
    // open succeeds ?
    ifstream file {fname, ios_base::binary};
    if (!file) throw system_error{errno, generic_category()};

    // sending WRITE
    string command {make_command(Flags::write, fname)};
    cout << "Sending command: " << command;
    send_request(socket, command);

    // get response from server
    string response {recieve_response(socket, status_messages[100].length())};
    cout << "Response: " << response << '\n';

    // server cannot open file
    if (response == status_messages[101]) {
        cerr << "File on server cannot be opened\n";
        return;
    }

    // transmit can begin
    if (response == status_messages[100]) {
        cout << "Begin file transfer\n";
        send_file(socket, file);
        cout << "File transfer completed\n";
        recieve_transfer_completed_message(socket);
        return;
    }

    throw runtime_error{"Server sent invalid response: " + response};
}

int main(int argc, char* argv[]) 
try {
    Args a {args(argc, argv)};
    check_arguments(a);

    Socket client {create_socket()};
    connect_to_server(client, a.host, to<uint16_t>(a.port));

    // choose action
    if (a.flags == Flags::write) { 
        write_file_to_server(client, a.file); 
    } else if (a.flags == Flags::read) {
        read_file_from_server(client, a.file);
    }
} catch (const exception& e) {
    string s {e.what()};
    remove_trailing_rn(s);
    cerr << "Exception has been thrown: " << s << '\n';
    return 1;
}

void check_flag(Flags f) { if (f != Flags::none) throw runtime_error{"check_flag(): read or write is already set!"}; }

Args args(int argc, char* argv[]) {
    Args a;

    for(int c {}; (c = getopt(argc, argv, "h:p:rw")) != -1;) {
        switch (c) {
            case 'h': a.host = optarg; break;
            case 'p': a.port = optarg; check_negative(a.port); break;
            case 'r': check_flag(a.flags); a.flags = Flags::read; break;
            case 'w': check_flag(a.flags); a.flags = Flags::write; break;
            case '?': throw runtime_error{""};
        }
    }

    if (optind < argc) a.file = argv[optind];
    if (optind+1 < argc) throw runtime_error{"args(): trailing file names after the file is set!"};

    return a;
}

void check_arguments(const Args& a) {
    if (a.host.empty()) throw runtime_error{"main(): host not set!"};
    if (a.port.empty()) throw runtime_error{"main(): port not set!"};
    if (a.flags == Flags::none) throw runtime_error{"main(): [-w|-r] not set!"};
    if (a.file.empty()) throw runtime_error{"main(): file not set!"};
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

void recieve_transfer_completed_message(int socket) {
    string response {recieve_response(socket, status_messages[102].length())};
    if (response != status_messages[102]) {
        throw runtime_error{"Server did not confirmed that file transfer is completed, this message was sent instead: " + response}; 
    }
}

void read_file_from_server(int socket, const string& fname) {
    string command {make_command(Flags::read, fname)};
    cout << "Sending command: " << command;
    send_request(socket, command);
    string response {recieve_response(socket, status_messages[100].length())};

    cout << "Response: " << response << '\n';
    if (response == status_messages[100]) {
        if (!fname.empty() && fname.front() == '/') {
            cerr << "Cannot access root directory: " << fname << '\n';
            send_response(socket, status_messages[101]);
            return;
        }

        auto p = get_path_and_file(fname);

        string path;
        for (const string& dir : p.first) {
            path += dir + '/';
            if (mkdir(path.c_str(), 0777) < 0) {
                perror("mkdir");
                send_response(socket, status_messages[101]);
                return;
            }
        }

        cout << "Begin file transfer\n";
        ofstream file {fname, ios_base::binary};
        if (!file) throw runtime_error{"Cannot open file \"" + fname + "\": " + error_code{errno, system_category()}.message()};
        recieve_and_write_file(socket, file);
        recieve_transfer_completed_message(socket);
        cout << "File transfer completed\n";
        return;
    }

    if (response == status_messages[101]) {
        cerr << "File cannot be opened\n";
        return;
    }

    throw runtime_error{"Server sent invalid response: " + response};
}
