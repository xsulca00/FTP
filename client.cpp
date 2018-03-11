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

using namespace std;

enum class Flags { none, read, write };

struct Args {
    string host {};
    string port {};
    Flags flags {Flags::none};
    string file;
}; 

using Buffer = array<char, 4096>;

struct Chunk {
    Buffer data;
    long size {};
};

struct Header {
    int length {};
    bool last {};
};

class Socket {
public:
    Socket(int s) : socket{s} { if (socket <= 0) throw runtime_error{"Invalid socket!"}; }
    ~Socket() { close(socket); }

    operator int() { return socket; }
private:
    int socket;
};


map<int, string> status_messages {
    {100, "100 Begin file transfer\r\n"},
    {101, "101 Can not open a file\r\n"},
    {102, "102 File transfer completed\r\n"}
};

void check_negative(const string& s) { if (!s.empty() && s.front() == '-') throw runtime_error{"Port number is negative!"}; }
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

template<typename Target = string, typename Source = string>
Target to(Source arg)
{
    stringstream s;
    Target t;

    if (!(s << arg) || !(s >> t) || !(s >> ws).eof())
        throw runtime_error {"to<>() failed!"};

    return t;
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

int create_socket() { return socket(AF_INET, SOCK_STREAM, 0); }

void send_request(int socket, const string& command) {
    ssize_t bytes {send(socket, command.c_str(), command.length(), 0)};
    if (bytes < 0) throw system_error{errno, generic_category()};
}

string recieve_response(int socket, ssize_t len) {
    string s(len, '\0');

    ssize_t bytes {};
    char* b {&s[0]};
    for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, b, remainder, 0);
        if (bytes < 0) throw system_error{errno, generic_category()};
        b += bytes;
    }

    return s;
}

Header get_header(int socket) {
    ssize_t bytes {};
    Header h;
    for (ssize_t remainder {sizeof(h)}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, &h, remainder, 0);
        cout << "get_header: sizeof(header): " << sizeof(h) << '\n';
        cout << "get_header: bytes: " << bytes << '\n';
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
        if (bytes < 0) throw system_error{errno, generic_category()};
        cout << "Data: " << string{b.begin(), b.begin() + bytes} << '\n';
        if (last) return;
    }
}

vector<Chunk> recieve_file(int socket) {
    vector<Chunk> chunks;
    Header h;
    Chunk c;
    cout << "Recieving file\n";
    while (!h.last) {
        cout << "Getting header\n";
        h = get_header(socket);
        if (h.length <= 0) throw runtime_error{"Header length=(" + to_string(h.length) + ") <= 0"};

        cout << "h.length = " << h.length << " h.last = " << h.last << '\n';

        chunks.push_back(Chunk{});
        cout << "Getting chunk\n";
        get_chunk(socket, chunks.back().data, h.length, h.last);
        chunks.back().size = h.length;
    }
    cout << "Chunks return\n";
    return chunks;
}

void write_data_to_file(const string& name, const vector<Chunk>& data) {
    ofstream file {name, ios_base::binary};
    if (!file) throw runtime_error{"Cannot open file: \"" + name + '"'};
    for (const Chunk& c : data) file.write(c.data.data(), c.size);
}

void read_file_from_server(int socket, const Args& a) {
    string command {make_command(Flags::read, a.file)};
    cout << "Sending command: " << command;
    send_request(socket, command);
    string response {recieve_response(socket, status_messages[100].length())};

    cout << "Response: " << response << '\n';
    if (response == status_messages[100]) {
        cout << "Begin file transfer\n";
        vector<Chunk> file_data {recieve_file(socket)};
        write_data_to_file(a.file, file_data);

        string response1 {recieve_response(socket, status_messages[102].length())};
        if (response1 != status_messages[102]) {
            throw runtime_error{"Server did not confirmed that file transfer is completed, this message was sent instead: " + response1}; 
        }
        cout << "File transfer completed\n";
        return;
    }

    if (response == status_messages[101]) {
        cerr << "File cannot be opened\n";
        return;
    }

    throw runtime_error{"Server sent invalid response: " + response};
}

hostent* get_host(const string& name) {
    hostent* host {gethostbyname(name.c_str())};
    if (!host) throw runtime_error{"Host with name " + name + " not found!"};
    return host;
}

sockaddr_in server_address(const hostent* host, uint16_t port) {
    sockaddr_in server {};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    copy_n((char*)host->h_addr, host->h_length, (char*)&server.sin_addr.s_addr);
    return server;
}

int main(int argc, char* argv[]) 
try {
    Args a {args(argc, argv)};
    check_arguments(a);

    Socket client {create_socket()};
    hostent* host {get_host(a.host)};
    sockaddr_in server {server_address(host, to<uint16_t>(a.port))};

    if (connect(client, (sockaddr*)&server, sizeof(server))) throw system_error{errno, generic_category()};

    // choose action
    if (a.flags == Flags::write) {} //write_file_to_server(client, a);
    else if (a.flags == Flags::read) read_file_from_server(client, a);
} catch (const exception& e) {
    string s {e.what()};
    auto pos = s.find_last_of('\r');
    if (pos != string::npos) s.erase(pos, pos+1);
    pos = s.find_last_of('\n');
    if (pos != string::npos) s.erase(pos, pos+1);

    cerr << "Exception has been thrown: " << s << '\n';
    return 1;
}
