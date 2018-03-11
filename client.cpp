extern "C" {
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
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
using namespace network::client;
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
    {102, "102 File transfer completed\r\n"}
};

void check_flag(Flags f);
Args args(int argc, char* argv[]);
void check_arguments(const Args& a);
string make_command(Flags f, const string& file);
void recieve_transfer_completed_message(int socket);
void read_file_from_server(int socket, const string& file);

int main(int argc, char* argv[]) 
try {
    Args a {args(argc, argv)};
    check_arguments(a);

    Socket client {create_socket()};
    connect_to_server(client, a.host, to<uint16_t>(a.port));

    // choose action
    if (a.flags == Flags::write) {} //write_file_to_server(client, a);
    else if (a.flags == Flags::read) read_file_from_server(client, a.file);
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
    string response1 {recieve_response(socket, status_messages[102].length())};
    if (response1 != status_messages[102]) {
        throw runtime_error{"Server did not confirmed that file transfer is completed, this message was sent instead: " + response1}; 
    }
}

void read_file_from_server(int socket, const string& file) {
    string command {make_command(Flags::read, file)};
    cout << "Sending command: " << command;
    send_request(socket, command);
    string response {recieve_response(socket, status_messages[100].length())};

    cout << "Response: " << response << '\n';
    if (response == status_messages[100]) {
        cout << "Begin file transfer\n";
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
