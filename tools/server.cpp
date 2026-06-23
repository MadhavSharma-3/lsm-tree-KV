#include "../include/db.h"
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib") 
#endif

using namespace std;

void handle_client(SOCKET client_socket, DB* db) {
    char buffer[4096];
    string stream_buffer = ""; // The persistent TCP frame buffer

    while (true) {
        memset(buffer, 0, 4096);
        int bytes_received = recv(client_socket, buffer, 4096, 0);
        if (bytes_received <= 0) break; 

        // Append raw bytes to our persistent stream
        stream_buffer.append(buffer, bytes_received);

        size_t pos;
        // Process EVERY command currently trapped in the buffer before waiting for more network traffic
        while ((pos = stream_buffer.find('\n')) != string::npos) {
            string request = stream_buffer.substr(0, pos);
            stream_buffer.erase(0, pos + 1); // Strip the processed command

            if (request.empty() || request == "\r") continue;

            stringstream ss(request);
            string command, key, value;
            ss >> command;

            for (auto& c : command) c = toupper(c);

            string response;
            if (command == "PUT") {
                ss >> key >> value;
                if (key.empty() || value.empty()) {
                    response = "-ERR wrong number of arguments for 'PUT'\n";
                } else {
                    db->put(key, value);
                    response = "+OK\n";
                }
            } else if (command == "GET") {
                ss >> key;
                string out_val;
                if (db->get(key, out_val)) {
                    response = out_val + "\n";
                } else {
                    response = "(nil)\n";
                }
            } else if (command == "DEL" || command == "DELETE") {
                ss >> key;
                if (db->remove(key)) {
                    response = ":1\n";
                } else {
                    response = ":0\n";
                }
            } else {
                response = "-ERR unknown command\n";
            }

            send(client_socket, response.c_str(), response.length(), 0);
        }
    }
    closesocket(client_socket);
}

int main() {
    string db_dir = "./server_data";
    if (!filesystem::exists(db_dir)) {
        filesystem::create_directories(db_dir);
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "[FATAL] Winsock initialization failed." << endl;
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, SOMAXCONN);

    cout << "StrataKV Database Engine booted." << endl;
    cout << "Listening for TCP connections on Port 8080..." << endl;
    
    DB db(db_dir);

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket != INVALID_SOCKET) {
            thread(handle_client, client_socket, &db).detach();
        }
    }

    WSACleanup();
    return 0;
}