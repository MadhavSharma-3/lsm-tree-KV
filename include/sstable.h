#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <mutex>
#include <memory> 
#include "bloom.h"

// Forward declaration to avoid circular header dependencies.
class Skiplist; 

// The structural blueprint for your Sparse Index.
struct IndexEntry {
    std::string key;
    uint32_t offset; 
};

class SSTable {
private:
    std::string file_path;
    std::ifstream table_file;
    std::mutex file_mutex; 
    
    std::vector<IndexEntry> sparse_index;
    std::unique_ptr<BloomFilter> filter; // Add the filter memory pointer
    void load_index();
    uint32_t num_entries; 

public:
    SSTable(const std::string& path);
    ~SSTable();

    uint32_t get_entry_count() const; 
    bool get(const std::string& key, std::string& out_value);

    static void flush_memtable_to_disk(Skiplist* memtable, const std::string& output_path);
};


// --- PHASE 4 ADDITION: The Sequential Reader ---
class SSTableIterator {
private:
    std::ifstream stream;
    uint32_t data_end_offset;

public:
    SSTableIterator(const std::string& path);
    ~SSTableIterator();
    
    // Checks if the read cursor has hit the sparse index
    bool has_next();
    
    // Extracts the next block and advances the cursor
    bool next(std::string& out_key, std::string& out_val);
};