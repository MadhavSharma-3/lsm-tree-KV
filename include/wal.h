#pragma once
#include <string>
#include <fstream>
#include <mutex>

// Defines the operation marker for the binary layout
enum class OpType : char {
    DELETE = 0,
    PUT = 1
};

class WAL {
private:
    std::string file_path;
    std::ofstream log_file;
    
    // The critical traffic light. This prevents the AAAAABBBBB interleaving 
    // when multiple threads hit the engine simultaneously.
    std::mutex wal_mutex; 

public:
    WAL(const std::string& path);
    ~WAL();

    // Serializes the data, acquires the lock, and safely appends it to the disk.
    void append(OpType type, const std::string& key, const std::string& value);
};