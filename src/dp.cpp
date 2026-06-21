#include "../include/db.h"

using namespace std;

DB::DB(const string& wal_path) {
    // Initialize the underlying data structures
    memtable = make_unique<Skiplist>(16, 0.5f);
    wal = make_unique<WAL>(wal_path);

    // TODO in Phase 2: Read the WAL file here and call memtable->insert() 
    // for every record to recover state after a crash.
}

void DB::put(const string& key, const string& value) {
    // 1. Acquire EXCLUSIVE Write Lock.
    // No other thread can read or write until this is done.
    unique_lock<shared_mutex> lock(db_mutex);

    // 2. Guarantee Durability First
    wal->append(OpType::PUT, key, value);

    // 3. Update In-Memory State
    memtable->insert(key, value);
}

bool DB::get(const string& key, string& out_value) {
    // 1. Acquire SHARED Read Lock.
    // Multiple threads can hold this lock simultaneously.
    // But if a thread holds a unique_lock (Write), this will block and wait.
    shared_lock<shared_mutex> lock(db_mutex);

    // 2. Fetch from memory
    return memtable->search(key, out_value);
}

bool DB::remove(const string& key) {
    // 1. Acquire EXCLUSIVE Write Lock.
    unique_lock<shared_mutex> lock(db_mutex);

    // 2. Check if it actually exists in memory before appending a useless delete log
    string temp;
    if (!memtable->search(key, temp)) {
        return false;
    }

    // 3. Guarantee Durability of Deletion
    wal->append(OpType::DELETE, key, "");

    // 4. Erase from Memory
    return memtable->remove(key);
}