#include "../include/sstable.h"
#include "../include/skiplist.h"
#include <iostream>
#include <algorithm>
// #include <mutex> 

// seekg : changes the reading pt. (seek get) 
// tellg : tells the reading point.  (tell get) 
// tellp : tells the writing point. 
// seekp : chnages the writing point. (seek put)

using namespace std;

void SSTable:: load_index(){
    // 8-byte trailer reading: [bloom_offset (4 bytes)] [index_offset (4 bytes)] [total entires cnt]
    table_file.seekg(-12, ios::end); 

    uint32_t index_offset, bloom_offset; 
    table_file.read(reinterpret_cast<char*>(&bloom_offset) , sizeof(uint32_t)); 
    table_file.read(reinterpret_cast<char*>(&index_offset) , sizeof(uint32_t)); 
    table_file.read(reinterpret_cast<char*>(&num_entries), sizeof(uint32_t));

    table_file.seekg(index_offset, ios::beg);

// push the sparse table from file to ram object. 
// its like [key_len][key][offset] with keylen, offset 4byte words
    while(table_file.tellg() < bloom_offset){
        uint32_t key_len; 
        if (!table_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) break; 

        string key(key_len,'\0'); 
        if(!table_file.read(&key[0], key_len)) break; 

        uint32_t offset; 
        if (!table_file.read(reinterpret_cast<char*>(&offset), sizeof(uint32_t)) ) break; 

        sparse_index.push_back({key, offset}); 
    }

// Read the Bloom Filter from disk into RAM
    table_file.seekg(bloom_offset, ios::beg);
    uint32_t bits, hashes, data_size;
// stored like : [bit_ct] [hash_ct] [len] [bit_array]
    table_file.read(reinterpret_cast<char*>(&bits), sizeof(uint32_t));
    table_file.read(reinterpret_cast<char*>(&hashes), sizeof(uint32_t));
    table_file.read(reinterpret_cast<char*>(&data_size), sizeof(uint32_t));

    vector<uint8_t> raw_data(data_size);
    table_file.read(reinterpret_cast<char*>(raw_data.data()), data_size);
    
    filter = make_unique<BloomFilter>(raw_data, bits, hashes);
}; 


SSTable:: SSTable(const string& ss_path){
    // open the table file to push the skiplist into it if it >= 4mb. 
    file_path = ss_path; 
    table_file.open(file_path, ios::binary); 
    num_entries = 0; 

    if (table_file.is_open()) {
        load_index();
    }
}; 


SSTable:: ~SSTable(){
    if (table_file.is_open()){
        table_file.close(); 
    }
}; 

uint32_t SSTable::get_entry_count() const {
    return num_entries;
}; 


bool SSTable:: get(const string& target_key, string& out_value){
    if (sparse_index.empty() || !table_file.is_open()) return false;

    // OP GUARD: Instantly kill the disk read if the key mathematically cannot exist.
    if (!filter->possibly_contains(target_key)) {
        return false;
    }

    // binary search the sparse table for {key, -2e9}
    auto it = lower_bound(sparse_index.begin(), sparse_index.end(), target_key, 
        [](const IndexEntry& a , const string& b){
            return a.key < b; 
        }); 
    
    // sparse out of bound handling
    if (it != sparse_index.begin() && (it == sparse_index.end() || it->key > target_key)) {
        --it;
    }
    if (it == sparse_index.end()) return false;


    std::lock_guard<std::mutex> lock(file_mutex);
// Clear any lingering error/EOF flags from previous thread operations
    table_file.clear(); 
    table_file.seekg(it->offset, ios::beg);       

    // iterate over the file until we hit that key 
    while(true){
        uint32_t key_len; 
        if (!table_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) return false; 

        string key(key_len,'\0'); 
        if (!table_file.read(&key[0], key_len)) return false; 

        uint32_t val_len;  
        if (!table_file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t))) return false;

        string val(val_len, '\0'); 
        if (!table_file.read(&val[0], val_len)) return false; 

        if (key == target_key){
            out_value = val ;
            return true; 
        }else if (key > target_key){
            return false; 
        }
    }
    
    return false; 
}; 



// flushes the skiplist into disk once 4mb of wal is complete. 
// outpath is the sstable path
void SSTable::flush_memtable_to_disk(Skiplist* memtable, const string& output_path) {
    
    ofstream out(output_path, ios::binary);
    if (!out.is_open()) return;

    // Allocate a filter anticipating roughly ~50,000 keys for a 4MB MemTable limit
    BloomFilter temp_filter(50000, 0.01);
    vector<IndexEntry> temp_index;
    Node* current = memtable->head->next[0];
    
    uint32_t bytes_written_since_last_index = 0;
    uint32_t entry_count = 0;
    const uint32_t INDEX_INTERVAL = 4096; // Create a RAM index marker every 4KB


    while (current != nullptr) {
        entry_count++; 
        uint32_t current_offset = out.tellp();

// If we crossed the 4KB interval, record the key and its exact byte offset
        if (temp_index.empty() || bytes_written_since_last_index >= INDEX_INTERVAL) {
            temp_index.push_back({current->key, current_offset});
            bytes_written_since_last_index = 0;
        }
        
// add to the filter
        temp_filter.add(current -> key); 

        uint32_t key_len = current->key.size();
        uint32_t val_len = current->value.size();

// write key value pairs into sstable
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        out.write(current->key.c_str(), key_len);
        
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(uint32_t));
        out.write(current->value.c_str(), val_len);

//  update the no of bytes
        bytes_written_since_last_index += (sizeof(uint32_t) * 2) + key_len + val_len;
        
        current = current->next[0];
    }



// append the sparse index to eof 
    uint32_t index_start_offset = out.tellp();

    for (const auto& entry : temp_index) {
        uint32_t key_len = entry.key.size();
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        out.write(entry.key.c_str(), key_len);
        out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(uint32_t));
    }

// append the bloom filter after the sparse index
    uint32_t bloom_start_offset = out.tellp();
    uint32_t bits = temp_filter.get_num_bits();
    uint32_t hashes = temp_filter.get_num_hashes();
    const auto& raw_data = temp_filter.get_raw_data();
    uint32_t data_size = raw_data.size();

    out.write(reinterpret_cast<const char*>(&bits), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&hashes), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(raw_data.data()), data_size);


// append the 8Byte trailer for sparse-index-table lookup
    out.write(reinterpret_cast<const char*>(&bloom_start_offset), sizeof(uint32_t)); 
    out.write(reinterpret_cast<const char*>(&index_start_offset), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&entry_count), sizeof(uint32_t));

    out.flush();
    out.close();
}





// ============================================================================
// SSTABLE ITERATOR IMPLEMENTATION 
// ============================================================================

SSTableIterator::SSTableIterator(const string& path) {
    stream.open(path, ios::binary);
    if (!stream.is_open()) return;

    // We must stop reading sequential data the exact byte the Sparse Index starts.
    // The offset of the sparse index is stored in the last 4 bytes of the file.
    stream.seekg(-8, ios::end);
    stream.read(reinterpret_cast<char*>(&data_end_offset), sizeof(uint32_t));
    
    // Reset the disk head to byte 0 to begin sequential reads.
    stream.seekg(0, ios::beg);
}

SSTableIterator::~SSTableIterator() {
    if (stream.is_open()) {
        stream.close();
    }
}

bool SSTableIterator::has_next() {
    if (!stream.is_open()) return false;
    // If our current position is less than the byte where the index begins, we have data.
    return stream.tellg() < data_end_offset;
}

bool SSTableIterator::next(string& out_key, string& out_val) {
    if (!has_next()) return false;

    // Strict boundaries. If a read fails, the disk file is corrupted or truncated.
    uint32_t key_len;
    if (!stream.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) return false;

    out_key.resize(key_len);
    if (!stream.read(&out_key[0], key_len)) return false;

    uint32_t val_len;
    if (!stream.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t))) return false;

    out_val.resize(val_len);
    if (!stream.read(&out_val[0], val_len)) return false;

    return true;
}
