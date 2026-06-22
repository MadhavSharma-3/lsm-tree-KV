#include "../include/db.h"
#include <fstream> 
#include <iostream> 

using namespace std;


DB::DB(const string& db_directory): db_dir(db_directory) {
    // Initialize the underlying data structures
    memtable = make_unique<Skiplist>(16, 0.75f);
    wal = make_unique<WAL>(db_dir + "/wal.bin");

    memtable_size = 0; 
    sstable_ct = 0; 

    while(true){
        string ss_path = db_dir + "/sstable" + to_string(sstable_ct) + ".bin"; 
        ifstream check(ss_path); 
        if (!check.good()) break ; 
        check.close(); 

        sstables.push_back(make_unique<SSTable>(ss_path));
        sstable_ct++;
    }

    // for the last record recover state after a crash.
    recover(); 
}


void DB::recover(){
    ifstream log_file(db_dir + "/wal.bin", ios::binary);
    
    // If the file doesn't exist, this is a fresh database. 
    if (!log_file.is_open()) {
        return; 
    }

    // peek() looks at the next byte without extracting it. 
    // This safely checks for EOF before attempting a read.
    while (log_file.peek() != EOF) {

        // 1. Read 1 byte for OpType
        char op_char;
        if (!log_file.read(&op_char, sizeof(char))) break;
        OpType op = static_cast<OpType>(op_char);

        // 2. Read 4 bytes directly into the memory address of key_len
        uint32_t key_len;
        if (!log_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) break;

        // 3. Pre-allocate string memory, then dump bytes directly into it
        string key(key_len, '\0');
        if (!log_file.read(&key[0], key_len)) break;

        // 4. Read 4 bytes for val_len
        uint32_t val_len;
        if (!log_file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t))) break;

        // 5. Pre-allocate string memory, dump value bytes
        string val(val_len, '\0');
        if (!log_file.read(&val[0], val_len)) break;

        // 6. Rebuild the MemTable state
        if (op == OpType::PUT || op == OpType::DELETE) {
            memtable->insert(key, val); 
            memtable_size += key.size() + val.size() + 32; 
        }
    }
}


void DB::flush_and_reset(){
    string ss_path = db_dir + "/sstable" + to_string(sstable_ct) + ".bin"; 

    SSTable::flush_memtable_to_disk(memtable.get(), ss_path); 
    sstables.push_back(make_unique<SSTable>(ss_path)); 
    sstable_ct++; 

    // FIX: Actually allocate a new SkipList to drop the old RAM payload
    memtable = make_unique<Skiplist>(16, 0.75f);
    memtable_size = 0; 

    // empty the current wal
    wal->clear(); 
}


void DB::put(const string& key, const string& value) {
    // 1. Acquire EXCLUSIVE Write Lock.
    // No other thread can read or write until this is done.
    unique_lock<shared_mutex> lock(db_mutex);

    if (memtable_size > MEMTABLE_LIMIT){
        flush_and_reset(); 
    }

    wal->append(OpType::PUT, key, value);
    memtable->insert(key, value);
    
    // change the memtable size
    memtable_size += key.size() + value.size() + 32; 
}


bool DB::get(const string& key, string& out_value) {
    // 1. Acquire SHARED Read Lock.
    // Multiple threads can hold this lock simultaneously.
    // But if a thread holds a unique_lock (Write), this will block and wait.
    shared_lock<shared_mutex> lock(db_mutex);
    
    // fetch the get request
    if (memtable->search(key, out_value)){
        return out_value != "<TOMBSTONE>"; 
    }

    for(int i = sstable_ct-1; i >= 0; i--){
        if (sstables[i]->get(key, out_value)) {
            return out_value != "<TOMBSTONE>";
        }
    }

    return false;  
}

bool DB::remove(const string& key) {
    // 1. Acquire EXCLUSIVE Write Lock.
    unique_lock<shared_mutex> lock(db_mutex);
    
    // 2. Check if it actually exists in memory before appending a useless delete log
    string temp;
    
    // FIX: A key might exist on disk but not in RAM. Bypassing deletion here is an error.
    // We must unconditionally append the TOMBSTONE to override any potential disk records.
    if (memtable_size > MEMTABLE_LIMIT){
        flush_and_reset(); 
    }
    
    // 3. Guarantee Durability of Deletion
    wal->append(OpType::DELETE, key, "<TOMBSTONE>");
    memtable->insert(key, "<TOMBSTONE>");
    memtable_size += key.size() + 11 + 32; // 11 is the size of "<TOMBSTONE>"
    
    return true; 
}