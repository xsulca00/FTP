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

using namespace std;

enum class Flags { none, read, write };

struct Args {
    string port {};
}; 

void check_negative(const string& s) {
    if (!s.empty() && s.front() == '-') throw runtime_error{"Port number is negative!"};
}

void check_flag(Flags f) {
    if (f != Flags::none) throw runtime_error{"check_flag(): read or write is already set!"};
}

Args args(int argc, char* argv[]) {
    Args a;

    for(int c {}; (c = getopt(argc, argv, "p:")) != -1;) {
        switch (c) {
            case 'p': a.port = optarg; check_negative(a.port); break;
            case '?': 
            {
                throw runtime_error{""};
            }
        }
    }

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
    Buffer data;
    long size {};
};

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


int create_socket() {
    int socket {::socket(AF_INET, SOCK_STREAM, 0)};
    if (socket <= 0) throw runtime_error{"Cannot create socket!"};
    return socket;
}

void send_response(int socket, const string& command) {
    ssize_t bytes {send(socket, command.c_str(), command.length(), 0)};
    if (bytes < 0) throw system_error{errno, generic_category()};
}

void send_bytes(int socket, const Chunk& c) {
    ssize_t bytes {send(socket, c.data.data(), c.size, 0)};
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

struct Header {
    int length {};
    bool last {};
};

Header get_header(int socket) {
    ssize_t bytes {};
    Header h;
    for (ssize_t remainder {sizeof(h)}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, &h, remainder, 0);
        if (bytes < 0) throw system_error{errno, generic_category()};
    }
    return h;
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

            /*
            string data {file_name.begin() + fname.size(), file_name.end()};
            cout << "Data: " << data << '\n';
            */
            if (!fname.empty()) {
                const string begin_file_transfer {"100 Begin file transfer\r\n"};
                const string cannot_open_file {"101 Cannot open file\r\n"};
                const string file_transfer_completed {"97 File transfer completed\r\n"};
                cout << "READ \"" << fname << "\"\n";
                // send prepare to recieve data
                ifstream file {fname, ios_base::binary};
                if (!file) {
                    send_response(socket, cannot_open_file);
                    throw runtime_error{"Cannot open requested file: " + fname + '\n'};
                }

                for (Chunk c; file;) {
                    file.read(c.data.data(), c.data.size());
                    c.size = file.gcount();
                    send_bytes(socket, c);
                }
            }
        }
    }
}

void get_chunk(int socket, Buffer& b, ssize_t len) {
    ssize_t bytes {};
    for (ssize_t remainder {len}; remainder > 0; remainder -= bytes) {
        bytes = recv(socket, b.data(), remainder, 0);
        if (bytes < 0) throw system_error{errno, generic_category()};
    }
}


vector<Chunk> recieve_file(int socket) {
    vector<Chunk> chunks;
    Header h;
    Chunk c;
    while (!h.last) {
        h = get_header(socket);
        if (h.length <= 0) throw runtime_error{"Header length=(" + to_string(h.length) + ") <= 0"};

        chunks.push_back(Chunk{});
        get_chunk(socket, chunks.back().data, h.length);
        chunks.back().size = h.length;
    }
    return chunks;
}

void write_data_to_file(const string& name, const vector<Chunk>& data) {
    ofstream file {name, ios_base::binary};
    if (!file) throw runtime_error{"Cannot open file: \"" + name + '"'};
    for (const Chunk& c : data) file.write(c.data.data(), c.size);
}

/*
void read_file_from_server(int socket, const Args& a) {
    const string begin_file_transfer {"100 Begin file transfer\r\n"};
    const string cannot_open_file {"101 Cannot open file\r\n"};

    send_request(socket, make_command(Flags::read, a.file));
    string response {recieve_response(socket, begin_file_transfer.length())};

    if (response == begin_file_transfer)  {
        vector<Chunk> file_data {recieve_file(socket)};
        write_data_to_file(a.file, file_data);

        const string file_transfer_completed {"97 File transfer completed\r\n"};
        string response1 {recieve_response(socket, file_transfer_completed.length())};
        if (response1 != file_transfer_completed) {
            throw runtime_error{"Server did not confirmed that file transfer is completed, this message was sent instead: " + response1}; 
        }
        return;
    }

    if (response == cannot_open_file) {
        cout << "File \"" << a.file << "\" cannot be transmitted\n";
        return;
    } 

    throw runtime_error{"Server sent invalid response: " + response};
}
*/

class Socket {
public:
    Socket(int s) : socket{s} { if (socket <= 0) throw runtime_error{"Invalid socket!"}; }
    ~Socket() { close(socket); }

    operator int() { return socket; }
private:
    int socket;
};


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

            /*
            string file;
            Flags flags;
            tie(flags, file) = get_request_type_and_file(request);
            switch (flags) {
                case Flags::read: cout << "READ " << file << '\n'; break; // prepare to send binary stream
                case Flags::write: cout << "WRITE " << file << '\n'; break; // prepare to accept binary stream
                default: throw runtime_error{"Invalid request type, not read nor write"};
            }
            */
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
