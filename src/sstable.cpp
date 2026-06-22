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
    // last 4 bytes of the file before eof is the offset. 
    table_file.seekg(-4, ios::end); 

    uint32_t index_offset; 
    table_file.read(reinterpret_cast<char*>(&index_offset) , sizeof(uint32_t)); 

    table_file.seekg(index_offset, ios::beg);
    
    uint32_t crnt_pos = table_file.tellg(); 
    table_file.seekg(0,ios::end); 
    uint32_t eof_pos = table_file.tellg(); 

    table_file.seekg(index_offset, ios::beg); 

// push the sparse table from file to ram object. 
// its like [key_len][key][offset] with keylen, offset 4byte words
    while(table_file.tellg() < eof_pos - 4){
        uint32_t key_len; 
        if (!table_file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t))) break; 

        string key(key_len,'\0'); 
        if(!table_file.read(&key[0], key_len)) break; 

        uint32_t offset; 
        if (!table_file.read(reinterpret_cast<char*>(&offset), sizeof(uint32_t)) ) break; 

        sparse_index.push_back({key, offset}); 
    }
}; 


SSTable:: SSTable(const string& ss_path){
    // open the table file to push the skiplist into it if it >= 4mb. 
    file_path = ss_path; 
    table_file.open(file_path, ios::binary); 
    if (table_file.is_open()) {
        load_index();
    }
}; 


SSTable:: ~SSTable(){
    if (table_file.is_open()){
        table_file.close(); 
    }
}; 


bool SSTable:: get(const string& target_key, string& out_value){
    if (sparse_index.empty() || !table_file.is_open()) return false;

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

    vector<IndexEntry> temp_index;
    Node* current = memtable->head->next[0];
    
    uint32_t bytes_written_since_last_index = 0;
    const uint32_t INDEX_INTERVAL = 4096; // Create a RAM index marker every 4KB


    while (current != nullptr) {
        uint32_t current_offset = out.tellp();

        // If we crossed the 4KB interval, record the key and its exact byte offset
        if (temp_index.empty() || bytes_written_since_last_index >= INDEX_INTERVAL) {
            temp_index.push_back({current->key, current_offset});
            bytes_written_since_last_index = 0;
        }

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

// append the 4byte trailer for sparse-index-table lookup
    out.write(reinterpret_cast<const char*>(&index_start_offset), sizeof(uint32_t));
    out.flush();
    out.close();
}