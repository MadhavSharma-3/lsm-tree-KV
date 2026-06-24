#pragma once
#include "skiplist.h"
#include "wal.h"
#include "sstable.h"
#include "lru_cache.h"
#include <string>
#include <vector>
#include <shared_mutex>
#include <memory>

class DB {
private:
    // everything is uniq_ptr. It handles destruction on its own. 
    std::unique_ptr<Skiplist> memtable;
    std::unique_ptr<WAL> wal;
    std::vector<std::unique_ptr<SSTable>> sstables;
    LRUCache row_cache; //cache jr. 

    // The Readers-Writer Lock. 
    std::shared_mutex db_mutex;

    // Flush threshold trackers
    const size_t MEMTABLE_LIMIT = 4*1024*1024; 
    size_t memtable_size;
    int sstable_ct; // Used to name files: sstable_1.bin, sstable_2.bin
    std:: string db_dir; //base directory path 
    
    // Internal Mechanics (Hidden from the user)
    void flush_and_reset();
    void recover(); 
    void compact(); 

public:
    DB(const std::string& db_directory);
    ~DB() = default;

    // The core public API
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& out_value);
    bool remove(const std::string& key);
};