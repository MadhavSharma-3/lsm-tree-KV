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

void WAL::append(OpType type, const string& key, const string& value) {
    // 1. Compute exact byte lengths
    uint32_t key_len = key.size();
    uint32_t val_len = value.size();

    // 2. THE CRITICAL SECTION (Concurrency Bottleneck)
    // The instant execution passes this line, this specific thread owns the file.
    // If 50 other threads hit this function simultaneously, they freeze right here 
    // and wait in a queue until this thread finishes and drops the lock.
    lock_guard<mutex> lock(wal_mutex);

    // 3. Binary Serialization
    // We do not write text. We write raw memory blocks.
    
    // Write 1 byte for the operation type (PUT or DELETE)
    char op = static_cast<char>(type);
    log_file.write(&op, sizeof(char));

    // Write exactly 4 bytes containing the integer length of the key
    // very agressive casting 
    log_file.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
    
    // Write the raw characters of the key
    log_file.write(key.c_str(), key_len);

    // Write exactly 4 bytes containing the integer length of the value
    log_file.write(reinterpret_cast<const char*>(&val_len), sizeof(uint32_t));
    
    // Write the raw characters of the value
    log_file.write(value.c_str(), val_len);

    // 4. Force OS to commit to magnetic disk / SSD
    // Without this, the OS caches the write in RAM, defeating the purpose of the log.
    log_file.flush(); 

    // The closing brace destroys `lock`. The file is released for the next thread.
}


void WAL:: clear(){
    lock_guard<mutex> lock(wal_mutex); 

    if (log_file.is_open()){
        log_file.close(); 
    }
 
    // / 2. Reopen with ios::trunc. 
    // This is the OS-level command to instantly truncate the file back to 0 bytes.
    log_file.open(file_path, ios::out | ios::binary | ios::trunc); 
    log_file.close(); 
    
    log_file.open(file_path, ios::app | ios::binary);
}