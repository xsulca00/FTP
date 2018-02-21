extern "C" {
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
}
#include <iostream>
#include <array>
#include <algorithm>
#include <string>
#include <sstream>

using namespace std;

enum class Flags { none, read, write };

struct Args {
    string host {};
    string port {};
    Flags flags {Flags::none};
    string file;
}; 

void check_negative(const string& s) {
    if (!s.empty() && s.front() == '-') throw runtime_error{"Port number is negative!"};
}

void check_flag(Flags f) {
    if (f != Flags::none) throw runtime_error{"check_flag(): read or write is already set!"};
}

Args args(int argc, char* argv[]) {
    Args a;

    for(int c {}; (c = getopt(argc, argv, "h:p:rw")) != -1;) {
        switch (c) {
            case 'h': a.host = optarg; break;
            case 'p': a.port = optarg; check_negative(a.port); break;
            case 'r': check_flag(a.flags); a.flags = Flags::read; break;
            case 'w': check_flag(a.flags); a.flags = Flags::write; break;
            case '?': 
            {
                throw runtime_error{""};
            }
        }
    }

    if (optind < argc) a.file = argv[optind];
    if (optind+1 < argc) throw runtime_error{"args(): trailing file names after the file is set!"};

    return a;
}

template<typename Target = string, typename Source = string>
Target to(Source arg)
{
    stringstream s;
    Target t;

    if (!(s << arg) || !(s >> t) || !(s >> ws).eof())
        throw runtime_error {"to<>() failed!"};

    return t;
}

int main(int argc, char* argv[]) {
    Args a {args(argc, argv)};

    if (a.host.empty()) throw runtime_error{"main(): host not set!"};
    if (a.port.empty()) throw runtime_error{"main(): port not set!"};
    if (a.flags == Flags::none) throw runtime_error{"main(): [-w|-r] not set!"};
    if (a.file.empty()) throw runtime_error{"main(): file not set!"};


    int client {socket(AF_INET, SOCK_STREAM, 0)};
    if (client <= 0) throw runtime_error{"Cannot create socket!"};

    hostent* host {gethostbyname(a.host.c_str())};
    if (!host) throw runtime_error{"Host with name " + a.host + " not found!"};
    
    sockaddr_in server {};
    server.sin_family = AF_INET;
    copy_n((char*)host->h_addr, host->h_length, (char*)&server.sin_addr.s_addr);

    server.sin_port = htons(to<uint16_t>(a.port));

    if (connect(client, (sockaddr*)&server, sizeof(server))) throw runtime_error{"Connection to host failed!"};

    ssize_t bytes {};
    array<char, 4096> buffer {};

    bytes = send(client, buffer.data(), buffer.size(), 0);
    if (bytes < 0) perror("sendto");

    bytes = recv(client, buffer.data(), buffer.size(), 0);
    if (bytes < 0) perror("recv");
    
}
