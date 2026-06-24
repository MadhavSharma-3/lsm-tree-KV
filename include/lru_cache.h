#pragma once
#include <string>
#include <list>
#include <unordered_map>
#include <mutex>

class LRUCache {
private:
    size_t capacity;
    
    // The Doubly-Linked List stores the actual Key-Value pairs.
    // Front = Most Recently Used, Back = Least Recently Used.

    // a fully associative cache. 
    std::list<std::pair<std::string, std::string>> items;
    
    // The Hash Map stores iterators pointing directly to the List nodes.
    // This allows O(1) lookups and O(1) list node removal.

    std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> map;
    
    // Dedicated mutex for cache thread safety. 
    // This lock is held for mere microseconds (RAM-only), preventing disk I/O stalls.
    std::mutex cache_mutex;

public:
    LRUCache(size_t max_items = 50000); // Default to caching 50k keys (~5-10MB RAM)
    
    bool get(const std::string& key, std::string& out_value);
    void put(const std::string& key, const std::string& value);
    void invalidate(const std::string& key);
    void clear();
};