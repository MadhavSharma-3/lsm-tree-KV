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

// blueprint of sparse-index
struct IndexEntry {
    std::string key;
    uint32_t offset; 
};

class SSTable {
private:
    std::string file_path;
    std::ifstream table_file;  //sstable file opened in input form (reading form).
    std::mutex file_mutex;  //mutex for thread safety
    
    //vector of indices with 4mb stride 
    std::vector<IndexEntry> sparse_index;

    std::unique_ptr<BloomFilter> filter; // Add the filter memory pointer
    void load_index(); //helper to construct the index table. 

    uint32_t num_entries; //count of total no of entries in the file.

public:
    SSTable(const std::string& path);
    ~SSTable();

    uint32_t get_entry_count() const; 
    bool get(const std::string& key, std::string& out_value);  //looks for the key in the file.

    static void flush_memtable_to_disk(Skiplist* memtable, const std::string& output_path); //flushes the ram to disk. 
};


// Sequential reader helper-class for the Garbage Collector. 
class SSTableIterator {
private:
    //ifstream is os-level object which opens a input pipeline to disk-file. 
    std::ifstream stream;
    uint32_t data_end_offset;

public:
    SSTableIterator(const std::string& path);
    ~SSTableIterator();
    
    // Checks if the read cursor has hit the end. 
    bool has_next();
    
    // Extracts the next block and advances the cursor.
    bool next(std::string& out_key, std::string& out_val);
};