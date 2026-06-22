#pragma once
#include "skiplist.h"
#include "wal.h"
#include "sstable.h"
#include <string>
#include <vector>
#include <shared_mutex>
#include <memory>

class DB {
private:
    std::unique_ptr<Skiplist> memtable;
    std::unique_ptr<WAL> wal;

    // The registry of all flushed disk files.
    std::vector<std::unique_ptr<SSTable>> sstables;

    // The Readers-Writer Lock. 
    std::shared_mutex db_mutex;

    // Flush threshold trackers
    const size_t MEMTABLE_LIMIT = 4*1024*1024; 
    size_t memtable_approx_size;
    int sstable_ct; // Used to name files: sstable_1.bin, sstable_2.bin

    void flush_and_reset();

public:
    DB(const std::string& wal_path);
    ~DB() = default;

    // The core public API
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& out_value);
    bool remove(const std::string& key);
    void recover(const string& wal_path); 
};