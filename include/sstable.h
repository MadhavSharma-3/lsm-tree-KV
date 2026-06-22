#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

// Forward declaration to avoid circular header dependencies.
class Skiplist; 

// The structural blueprint for your Sparse Index.
// This lives entirely in RAM for every SSTable file on disk.
struct IndexEntry {
    std::string key;
    uint32_t offset; // The exact byte address where this key's record starts in the file.
};

class SSTable {
private:
    std::string file_path;
    std::ifstream table_file;
    
    // The RAM-resident map. A 4MB file might only have a 40KB index.
    std::vector<IndexEntry> sparse_index;

    // Internal helper. Called strictly during initialization to rip the index 
    // off the tail end of the .bin file and load it into the vector above.
    void load_index();

public:
    // Opens the file in binary mode and immediately calls load_index().
    SSTable(const std::string& ss_path);
    ~SSTable();

    // The core read path.
    // 1. std::lower_bound on sparse_index.
    // 2. table_file.seekg(offset).
    // 3. Extract and return the value.
    bool get(const std::string& target_key, std::string& out_value);

    // The Phase 4 execution trigger.
    // Takes the frozen MemTable and streams Level 0 sequentially to the disk.
    // Appends the sparse index to the absolute EOF.
    static void flush_memtable_to_disk(Skiplist* memtable, const std::string& output_path);
};