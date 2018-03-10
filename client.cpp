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

using Buffer = array<char, 4096>;

struct Chunk {
    Chunk(const Buffer& d, int s) : data{d}, size{s} {}

    Buffer data;
    long size;
};

void check_arguments(const Args& a) {
    if (a.host.empty()) throw runtime_error{"main(): host not set!"};
    if (a.port.empty()) throw runtime_error{"main(): port not set!"};
    if (a.flags == Flags::none) throw runtime_error{"main(): [-w|-r] not set!"};
    if (a.file.empty()) throw runtime_error{"main(): file not set!"};
}

int create_socket() {
    int client {socket(AF_INET, SOCK_STREAM, 0)};
    if (client <= 0) throw runtime_error{"Cannot create socket!"};
    return client;
}

long check_response_and_get_filesize(const string& response) {
    size_t found {response.find("96 Begin file transfer. Size: ")};
    if (found == string::npos) throw runtime_error{"Invalid begin to transfer command sent by server: \"" + response + '"'};

    string file_size {response.substr(found+strlen("96 Begin file transfer. Size: "))};
    if (file_size.size() >= 2 && 
       (file_size.back() != '\n' ||  file_size[file_size.size()-2] != '\r')) throw runtime_error{"Invalid begin to transfer command sent by server: \"" + response + '"'};

    return stol(file_size);
}

bool recieve_bytes(int socket, Buffer& b) {
    ssize_t bytes {recv(socket, b.data(), b.size(), 0)};
    if (bytes < 0) perror("recv");
    return bytes < 0;
}

void read_file_from_server(int socket) {
    Buffer buffer;

    recieve_bytes(socket, buffer);

    ssize_t file_size {check_response_and_get_filesize(buffer.data())};

    vector<Chunk> data;
    for (ssize_t i {1}; i <= file_size; i += bytes) {
        recieve_bytes(socket, buffer);
        data.emplace_back(buffer, bytes);
    }

    {
        ofstream file {"file", ios_base::binary};
        if (!file) throw runtime_error{"Cannot open file"};
        for (const Chunk& c : data) {
            file.write(c.data.data(), c.size);
        }
    }

    recieve_bytes(socket, buffer);

    if (string{buffer.data(), buffer.size()}.find("97 File transfer completed\r\n") != string::npos) {
        throw runtime_error{"Server did not confirmed that file transfer is completed!"};
    }
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

int main(int argc, char* argv[]) {
    Args a {args(argc, argv)};
    check_arguments(a);

    int client {create_socket()};

    hostent* host {gethostbyname(a.host.c_str())};
    if (!host) throw runtime_error{"Host with name " + a.host + " not found!"};
    
    sockaddr_in server {};
    server.sin_family = AF_INET;
    copy_n((char*)host->h_addr, host->h_length, (char*)&server.sin_addr.s_addr);

    server.sin_port = htons(to<uint16_t>(a.port));

    if (connect(client, (sockaddr*)&server, sizeof(server))) throw runtime_error{"Connection to host failed!"};

    string command {make_command(a.flags, a.file)};

    cout << "Sending command: \"" << command << "\"\n";

    // sending command
    ssize_t bytes {send(client, command.c_str(), command.length(), 0)};
    if (bytes < 0) perror("sendto");

    // choose action
    if (a.flags == Flags::write) {
    } else if (a.flags == Flags::read) {
        read_file_from_server(client);
    }

    /*
    // wait for reply
    bytes = send(client, command.c_str(), command.length(), 0);
    if (bytes < 0) perror("sendto");

    // recieve bytes 
    bytes = recv(client, buffer.data(), buffer.size(), 0);
    if (bytes < 0) perror("recv");
    */
}
