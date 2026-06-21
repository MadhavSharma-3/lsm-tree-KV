#pragma once
#include "skiplist.h"
#include "wal.h"
#include <string>
#include <shared_mutex>
#include <memory>

class DB {
private:
    std::unique_ptr<Skiplist> memtable;
    std::unique_ptr<WAL> wal;

    // The Readers-Writer Lock. 
    // This protects the MemTable from in-memory race conditions.
    std::shared_mutex db_mutex;

public:
    DB(const std::string& wal_path);
    ~DB() = default;

    // The core public API
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& out_value);
    bool remove(const std::string& key);
};