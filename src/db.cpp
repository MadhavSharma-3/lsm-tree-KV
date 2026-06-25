#include "../include/db.h"
#include <iostream>
#include <queue> 
#include <filesystem> 
#include <fstream> 

using namespace std;
namespace fs = std::filesystem;

DB::DB(const string& db_directory): db_dir(db_directory), row_cache(50000) {
    
    // Initialize the underlying data structures
    memtable = make_unique<Skiplist>(16, 0.75f);
    wal = make_unique<WAL>(db_dir + "/wal.bin");

    memtable_size = 0; 
    sstable_ct = 0; 

    // look for previous sstables in the db_dir
    while(true){
        string ss_path = db_dir + "/sstable_" + to_string(sstable_ct) + ".bin"; 
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

        // Read 1 byte for OpType
        char op_char;
        if (!log_file.read(&op_char, sizeof(char))) break;
        OpType op = static_cast<OpType>(op_char);

        // Read 4 bytes directly into the memory address of key_len
        uint32_t key_len;
        if (!log_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) break;

        // Read the key
        string key(key_len, '\0');
        if (!log_file.read(&key[0], key_len)) break;

        // Read 4 bytes for val_len
        uint32_t val_len;
        if (!log_file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t))) break;

        // dump value bytes
        string val(val_len, '\0');
        if (!log_file.read(&val[0], val_len)) break;

        // Rebuild the MemTable state from wal. 
        if (op == OpType::PUT || op == OpType::DELETE) {
            memtable->insert(key, val); 
            memtable_size += key.size() + val.size() + 32; 
        }
    }
}


void DB::flush_and_reset(){
    string ss_path = db_dir + "/sstable_" + to_string(sstable_ct) + ".bin"; 

    SSTable::flush_memtable_to_disk(memtable.get(), ss_path); 
    sstables.push_back(make_unique<SSTable>(ss_path)); 
    sstable_ct++; 

    // allocate a new SkipList to drop the old RAM payload
    memtable = make_unique<Skiplist>(16, 0.75f);
    memtable_size = 0; 

    // empty the current wal
    wal->clear(); 

    // The GC Trigger Mechanism
    if (sstable_ct >= 4) {
        compact();
    }
}


void DB::put(const string& key, const string& value) {

    // No other thread can read or write until this is done.
    unique_lock<shared_mutex> lock(db_mutex);

    if (memtable_size > MEMTABLE_LIMIT){
        flush_and_reset(); 
    }

    wal->append(OpType::PUT, key, value);
    memtable->insert(key, value);
    
    // CACHE MECHANICS: We update the cache with the live value instead of invalidating.
    // When the MemTable flushes, this guarantees the cache already has the hottest data.
    row_cache.put(key, value);

    // change the memtable size
    memtable_size += key.size() + value.size() + 32; //each key with vector of next[]
}


bool DB::get(const string& key, string& out_value) {
    // Multiple threads can hold this lock simultaneously.
    // But if a thread holds a unique_lock (Write), this will block and wait.
    shared_lock<shared_mutex> lock(db_mutex);
    
    // fetch the get request
    if (memtable->search(key, out_value)){
        return out_value != "<TOMBSTONE>"; 
    }

    // OP GUARD : User-Space LRU Cache Lookup (Zero Syscalls, cached disk state)
    if (row_cache.get(key, out_value)) {
        return out_value != "<TOMBSTONE>";
    }

    for(int i = sstable_ct-1; i >= 0; i--){
        if (sstables[i]->get(key, out_value)) {
            // CACHE MECHANICS: On a successful disk fetch, pull the data into user-space RAM.
            row_cache.put(key, out_value);
            return out_value != "<TOMBSTONE>";
        }
    }

    return false;  
}

bool DB::remove(const string& key) {

    unique_lock<shared_mutex> lock(db_mutex);
    
    if (memtable_size > MEMTABLE_LIMIT){
        flush_and_reset(); 
    }
    
    // Fast write mechanics : dont' remove the key, mark it as TOMBSTONE. 
    // this prevents slow disk access. 
    wal->append(OpType::DELETE, key, "<TOMBSTONE>");
    memtable->insert(key, "<TOMBSTONE>");

    // CACHE MECHANICS:
    // By storing the tombstone in RAM, subsequent reads will instantly fail at OP GUARD
    // without triggering physical disk scans across SSTables.
    row_cache.put(key, "<TOMBSTONE>");

    memtable_size += key.size() + 11 + 32; // 11 is the size of "<TOMBSTONE>"
    
    return true; 
}




// A wrapper to help the priority queue keep track of which file a key came from
struct MergeNode {
    string key;
    string value;
    int sstable_index;

    // comparator function for the heap. 
    bool operator<(const MergeNode& other) const {
        if (key == other.key) {
            return sstable_index < other.sstable_index; 
        }
        return key > other.key; 
    }
};



// garbage collector to skim files if sstable_ct >= 4 .
void DB::compact() {
    if (sstables.size() <= 1) return;
    
// In a production engine, this runs on a detached thread.
// For structural correctness, we run it synchronously under the write lock.

// get the exact key count from sstables. 
    uint32_t exact_max_keys = 0;
    for (int i = 0; i < sstable_ct; i++) {
        exact_max_keys += sstables[i]->get_entry_count();
    }
    exact_max_keys = max(1u, exact_max_keys); 

    vector<unique_ptr<SSTableIterator>> iterators;
    for (int i = 0; i < sstable_ct; i++) {
        iterators.push_back(make_unique<SSTableIterator>(db_dir + "/sstable_" + to_string(i) + ".bin"));
    }

    priority_queue<MergeNode> heap;

// Seed the heap with the first element of each file
    for (int i = 0; i < iterators.size(); i++) {
        string k, v;
        if (iterators[i]->next(k, v)) {
            heap.push({k, v, i});
        }
    }

    string compacted_path = db_dir + "/compacted.bin";
    ofstream out(compacted_path, ios::binary);
    
// the compacted sparse table 
    vector<IndexEntry> temp_index;
    BloomFilter temp_filter(exact_max_keys, 0.01); // Allocate precisely based on metadata


    uint32_t compacted_entry_count = 0; // Track unique keys written to the dense file
    uint32_t bytes_written = 0;
    const uint32_t INDEX_INTERVAL = 4096;
    string last_key = "";


// implementing a k-way merge in sorted linkedlists using a heap. 
    while (!heap.empty()) {
        MergeNode current = heap.top();
        heap.pop();

        // Immediately pull the next sequential item from the file that just won
        string next_k, next_v;
        if (iterators[current.sstable_index]->next(next_k, next_v)) {
            heap.push({next_k, next_v, current.sstable_index});
        }

// The heap already fed us the newest version first. Drop this obsolete duplicate.
// we only need one last key because we are processing the whole thing in sorted order. 
        if (current.key == last_key) {
            continue;
        }
        last_key = current.key;

        // it means the key is deleted. Drop it completely to reclaim disk space.
        if (current.value == "<TOMBSTONE>") {
            continue;
        }

        compacted_entry_count++;

        // Write winning key-value pair to the dense new file
        uint32_t current_offset = out.tellp();
        if (temp_index.empty() || bytes_written >= INDEX_INTERVAL) {
            temp_index.push_back({current.key, current_offset});
            bytes_written = 0;
        }

        temp_filter.add(current.key); // add to bloom-filter

        uint32_t key_len = current.key.size();
        uint32_t val_len = current.value.size();

        out.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        out.write(current.key.c_str(), key_len);
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(uint32_t));
        out.write(current.value.c_str(), val_len);

        bytes_written += (sizeof(uint32_t) * 2) + key_len + val_len;
    }

// Append the sparse index.
    uint32_t index_start_offset = out.tellp();
    for (const auto& entry : temp_index) {
        uint32_t key_len = entry.key.size();
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        out.write(entry.key.c_str(), key_len);
        out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(uint32_t));
    }


// Append Bloom Filter Data
    uint32_t bloom_start_offset = out.tellp();
    uint32_t bits = temp_filter.get_num_bits();
    uint32_t hashes = temp_filter.get_num_hashes();
    const auto& raw_data = temp_filter.get_raw_data();
    uint32_t data_size = raw_data.size();

    out.write(reinterpret_cast<const char*>(&bits), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&hashes), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(raw_data.data()), data_size);


// Append the offset of 12 bytes 
    out.write(reinterpret_cast<const char*>(&bloom_start_offset), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&index_start_offset), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&compacted_entry_count), sizeof(uint32_t));

    out.flush();
    out.close();

    // Release memory handles, destroy old fragments, map the new file
    sstables.clear();
    iterators.clear();

    for (int i = 0; i < sstable_ct; i++) {
        fs::remove(db_dir + "/sstable_" + to_string(i) + ".bin");
    }

    fs::rename(compacted_path, db_dir + "/sstable_0.bin");
    sstable_ct = 1;
    sstables.push_back(make_unique<SSTable>(db_dir + "/sstable_0.bin"));
    
}