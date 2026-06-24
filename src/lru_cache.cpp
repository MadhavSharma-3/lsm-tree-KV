#include "../include/lru_cache.h"

using namespace std;
// A hash-Linked list implementation 

LRUCache::LRUCache(size_t max_items) : capacity(max_items) {}

bool LRUCache::get(const string& key, string& out_value) {
    lock_guard<mutex> lock(cache_mutex);
    
    auto it = map.find(key);
    if (it == map.end()) {
        return false; // Cache Miss
    }
    
    // Cache Hit. Move the accessed item to the front of the list (Most Recently Used).
    // splice() is an O(1) pointer rewiring operation. It does not copy data.
    items.splice(items.begin(), items, it->second);
    
    out_value = it->second->second;
    return true;
}

void LRUCache::put(const string& key, const string& value) {
    lock_guard<mutex> lock(cache_mutex);
    
    auto it = map.find(key);
    if (it != map.end()) {
        // Key exists. Update value and move to front.
        it->second->second = value;
        items.splice(items.begin(), items, it->second);
        return;
    }
    
    // Key does not exist. Check capacity.
    if (items.size() >= capacity) {
        // Evict the Least Recently Used item (the one at the back of the list)
        auto last = items.back();
        map.erase(last.first);
        items.pop_back();
    }
    
    // Insert new item at the front.
    items.emplace_front(key, value);
    map[key] = items.begin();
}

void LRUCache::invalidate(const string& key) {
    lock_guard<mutex> lock(cache_mutex);
    auto it = map.find(key);
    if (it != map.end()) {
        items.erase(it->second);
        map.erase(it);
    }
}

void LRUCache::clear() {
    lock_guard<mutex> lock(cache_mutex);
    items.clear();
    map.clear();
}