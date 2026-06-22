#include "../include/db.h"

using namespace std;

void DB::recover(const string& wal_path){
    ifstream log_file(wal_path, ios::binary);
    
    // If the file doesn't exist, this is a fresh database. Do nothing.
    if (!log_file.is_open()) {
        return; 
    }

    // peek() looks at the next byte without extracting it. 
    // This safely checks for EOF before attempting a read.
    while (log_file.peek() != EOF) {
        
        // 1. Read 1 byte for OpType
        char op_char;
        log_file.read(&op_char, sizeof(char));
        OpType op = static_cast<OpType>(op_char);

        // 2. Read 4 bytes directly into the memory address of key_len
        uint32_t key_len;
        log_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));

        // 3. Pre-allocate string memory, then dump bytes directly into it
        string key(key_len, '\0');
        log_file.read(&key[0], key_len);

        // 4. Read 4 bytes for val_len
        uint32_t val_len;
        log_file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t));

        // 5. Pre-allocate string memory, dump value bytes
        string val(val_len, '\0');
        log_file.read(&val[0], val_len);

        // 6. Rebuild the MemTable state
        if (op == OpType::PUT) {
            memtable->insert(key, val);
        } else if (op == OpType::DELETE) {
            memtable->remove(key);
        }
    }
}

DB::DB(const string& wal_path) {
    // Initialize the underlying data structures
    memtable = make_unique<Skiplist>(16, 0.5f);
    wal = make_unique<WAL>(wal_path);

    // TODO in Phase 2: Read the WAL file here and call memtable->insert() 
    // for every record to recover state after a crash.
    recover(wal_path); 

    
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