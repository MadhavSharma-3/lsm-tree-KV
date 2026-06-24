#include "../include/wal.h"
#include <iostream>
// #include <mutex> already in the header. 

using namespace std;

WAL::WAL(const string& path) : file_path(path) {
    // Open in append mode (never overwrite) and strict binary mode
    log_file.open(file_path, ios::app | ios::binary);
    if (!log_file.is_open()) {
        cerr << "FATAL: Could not open WAL file. Halting execution." << endl;
        exit(1); // A database without a WAL cannot guarantee state. Kill it.
    }
}

WAL::~WAL() {
    if (log_file.is_open()) {
        log_file.close();
    }
}


// educational information : 
    // static_cast<char>(type): This is a safe, mathematically verified cast. OpType::PUT is an enum class backed by a 1-byte character. 
    // The compiler knows they are identical in size, so static_cast simply tells the compiler: 
    // "Treat this enum label as its underlying 1-byte value so I can write it."

    // reinterpret_cast<const char*>(&key_len): This is the most dangerous and powerful cast in C++. key_len is a 4-byte uint32_t. 
    // The log_file.write() function absolutely demands a pointer to a char array. reinterpret_cast tells the compiler to completely abandon type safety. 
    // It says: "Take the memory address of this integer (&key_len), pretend it is actually
    // a pointer to an array of 4 characters (const char*), and sequentially copy those exact 4 bytes of raw RAM directly onto the SSD."

void WAL::append(OpType type, const string& key, const string& value) {

    uint32_t key_len = key.size();
    uint32_t val_len = value.size();

    // #thread lock
    lock_guard<mutex> lock(wal_mutex);


    // Write 1 byte for the operation type (PUT or DELETE)
    char op = static_cast<char>(type);
    log_file.write(&op, sizeof(char));

    // Write exactly 4 bytes for the key_len
    log_file.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
    
    // Write the raw characters of the key
    log_file.write(key.c_str(), key_len);

    // Write exactly 4 bytes for val_len
    log_file.write(reinterpret_cast<const char*>(&val_len), sizeof(uint32_t));
    
    // Write the raw characters of the value
    log_file.write(value.c_str(), val_len);

    // Force OS to commit to magnetic disk / SSD
    // Without this, the OS caches the write in RAM, defeating the purpose of the log.
    log_file.flush(); 

    // The closing brace destroys `lock`. The file is released for the next thread.
}


void WAL:: clear(){
    lock_guard<mutex> lock(wal_mutex); 

    if (log_file.is_open()){
        log_file.close(); 
    }
 
    // Reopen with ios::trunc. 
    // This is a OS-level command to instantly truncate the file back to 0 bytes.
    log_file.open(file_path, ios::out | ios::binary | ios::trunc); 
    log_file.close(); 
    
    log_file.open(file_path, ios::app | ios::binary);
}