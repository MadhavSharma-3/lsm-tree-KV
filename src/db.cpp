#include "../include/db.h"
#include <iostream>
#include <queue> 
#include <filesystem> 
#include <fstream> // REQUIRED FOR FILE I/O

using namespace std;
namespace fs = std::filesystem;

DB::DB(const string& db_directory): db_dir(db_directory) {
    // Initialize the underlying data structures
    memtable = make_unique<Skiplist>(16, 0.75f);
    wal = make_unique<WAL>(db_dir + "/wal.bin");

    memtable_size = 0; 
    sstable_ct = 0; 

    while(true){
        // FIXED: Added missing underscore to match compaction logic
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
    // FIXED: Added missing underscore
    string ss_path = db_dir + "/sstable_" + to_string(sstable_ct) + ".bin"; 

    SSTable::flush_memtable_to_disk(memtable.get(), ss_path); 
    sstables.push_back(make_unique<SSTable>(ss_path)); 
    sstable_ct++; 

    // FIX: Actually allocate a new SkipList to drop the old RAM payload
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




// A wrapper to help the priority queue keep track of which file a key came from
struct MergeNode {
    string key;
    string value;
    int sstable_index;

    // std::priority_queue is a max-heap. 
    // We invert the operators so the smallest string sits at the top (Min-Heap).
    // CRITICAL: If keys are identical, we force the HIGHER sstable_index (newer data) to the top.
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
    cout << "\n[Compaction] Merging " << sstables.size() << " fragmented SSTables. " << endl;


// --- PHASE 4 FIX: Precise Bloom Filter Sizing via Trailer Metadata ---
    // Extract exact physical entry counts from the target SSTable trailers.
    // This perfectly caps the Bloom Filter RAM allocation without heuristic guesswork.
    uint32_t exact_max_keys = 0;
    for (int i = 0; i < sstable_ct; i++) {
        exact_max_keys += sstables[i]->get_entry_count();
    }


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


    while (!heap.empty()) {
        MergeNode current = heap.top();
        heap.pop();

        // Immediately pull the next sequential item from the file that just won
        string next_k, next_v;
        if (iterators[current.sstable_index]->next(next_k, next_v)) {
            heap.push({next_k, next_v, current.sstable_index});
        }

        // Conflict Resolution: If this key matches the last processed key, it is an older version.
        // The heap already fed us the newest version first. Drop this obsolete duplicate.
// we only need one last key because we are processing the whole thing in sorted order. 
        if (current.key == last_key) {
            continue;
        }
        last_key = current.key;

        // Garbage Collection: If the reigning version of this key is a Tombstone, 
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

        temp_filter.add(current.key); // Ensure compacted keys hit the filter

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

    // The Atomic Swap: Release memory handles, destroy old fragments, map the new file
    sstables.clear();
    iterators.clear();

    for (int i = 0; i < sstable_ct; i++) {
        fs::remove(db_dir + "/sstable_" + to_string(i) + ".bin");
    }

    fs::rename(compacted_path, db_dir + "/sstable_0.bin");
    sstable_ct = 1;
    sstables.push_back(make_unique<SSTable>(db_dir + "/sstable_0.bin"));
    
    cout << "[Compaction] Complete. Deleted keys dropped. Disk space reclaimed." << endl;
}