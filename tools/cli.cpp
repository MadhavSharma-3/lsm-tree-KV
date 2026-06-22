#include "../include/db.h"
#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>

using namespace std;

int main() {
    string db_dir = "./data";

    // Structurally ensure the data directory exists before the DB tries to open files in it.
    if (!filesystem::exists(db_dir)) {
        filesystem::create_directories(db_dir);
    }

    cout << "\nBooting database Engine..." << endl;
    DB db(db_dir);
    cout << "Engine online. Type commands (PUT <key> <val>, GET <key>, DEL <key>, EXIT):" << endl;

    string line;
    while (true) {
        cout << "\n127.0.0.1:6379> ";
        if (!getline(cin, line)) break;

        if (line.empty()) continue;

        stringstream ss(line);
        string command, key, value;
        ss >> command;

        // Force command to uppercase 
        for (auto& c : command) c = toupper(c);

        if (command == "EXIT" || command == "QUIT") {
            cout << "Shutting down engine safely." << endl;
            break;
        }

        if (command == "PUT") {
            ss >> key >> value;
            if (key.empty() || value.empty()) {
                cout << "(error) ERR wrong number of arguments for 'PUT' command" << endl;
                continue;
            }
            db.put(key, value);
            cout << "OK" << endl;
        } 
        else if (command == "GET") {
            ss >> key;
            if (key.empty()) {
                cout << "(error) ERR wrong number of arguments for 'GET' command" << endl;
                continue;
            }
            
            string out_val;
            if (db.get(key, out_val)) {
                cout << "\"" << out_val << "\"" << endl;
            } else {
                cout << "(nil)" << endl;
            }
        } 
        else if (command == "DEL" || command == "DELETE") {
            ss >> key;
            if (key.empty()) {
                cout << "(error) ERR wrong number of arguments for 'DEL' command" << endl;
                continue;
            }
            
            if (db.remove(key)) {
                cout << "(integer) 1" << endl;
            } else {
                cout << "(integer) 0" << endl;
            }
        } 
        else {
            cout << "(error) ERR unknown command '" << command << "'" << endl;
        }
        cout << endl; 
    }

    return 0;
}