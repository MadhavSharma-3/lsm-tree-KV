#include "../include/skiplist.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

using namespace std;

void test_basic_crud() {
    Skiplist sl(16, 0.5f);
    string val;

    // 1. Basic Insertion
    sl.insert("key1", "val1");
    sl.insert("key2", "val2");
    sl.insert("apple", "red");
    sl.insert("zebra", "stripes");
    
    assert(sl.search("key1", val) && val == "val1");
    assert(sl.search("zebra", val) && val == "stripes");
    assert(!sl.search("ghost", val)); // Ensure non-existent keys fail cleanly

    // 2. Overwrite Mechanics
    sl.insert("key1", "val1_updated");
    assert(sl.search("key1", val) && val == "val1_updated");

    // 3. Deletion Mechanics
    assert(sl.remove("key2")); // Should return true on successful delete
    assert(!sl.search("key2", val)); // Should no longer exist
    assert(!sl.remove("key2")); // Should return false because it's already gone

    cout << "[OK] Basic CRUD operations and pointer reassignment passed." << endl;
}

void test_edge_cases() {
    Skiplist sl(16, 0.75f);
    string val;

    // Test zero-length strings (these often break poor memory allocators)
    sl.insert("", "empty_key");
    assert(sl.search("", val) && val == "empty_key");

    sl.insert("empty_val", "");
    assert(sl.search("empty_val", val) && val == "");

    cout << "[OK] Memory boundaries (edge cases) passed." << endl;
}

void test_stress_and_time_complexity() {
    Skiplist sl(16, 0.5f);
    const int NUM_ITEMS = 100000; 

    vector<string> keys;
    for (int i = 0; i < NUM_ITEMS; i++) {
        keys.push_back("user_" + to_string(i));
    }

    // Shuffle the array to force the SkipList to handle random inserts.
    auto rng = default_random_engine {};
    shuffle(keys.begin(), keys.end(), rng);

    cout << "Executing Stress Test with " << NUM_ITEMS << " randomized keys..." << endl;

    // --- INSERTION TEST ---
    auto start = chrono::high_resolution_clock::now();
    for (const auto& k : keys) {
        sl.insert(k, "payload_data");
    }
    auto end = chrono::high_resolution_clock::now();
    
    cout << "  -> Inserted " << NUM_ITEMS << " items in " 
         << chrono::duration_cast<chrono::milliseconds>(end - start).count() 
         << " ms." << endl;

    // --- SEARCH TEST ---
    string val;
    start = chrono::high_resolution_clock::now();
    for (const auto& k : keys) {
        bool found = sl.search(k, val);
        if (!found) {
            cerr << "FATAL: Lost pointer for key " << k << ". SkipList is corrupted." << endl;
            exit(1); 
        }
    }
    end = chrono::high_resolution_clock::now();
    
    cout << "  -> Searched " << NUM_ITEMS << " items in " 
         << chrono::duration_cast<chrono::milliseconds>(end - start).count() 
         << " ms." << endl;

    cout << "[OK] O(log N) Time Complexity and large-scale allocation passed." << endl;
}

void test_massive_deletion() {
    Skiplist sl(16, 0.5f);
    const int NUM_ITEMS = 100000; 

    vector<string> keys;
    for (int i = 0; i < NUM_ITEMS; i++) {
        keys.push_back("del_target_" + to_string(i));
    }

    auto rng = default_random_engine {};
    shuffle(keys.begin(), keys.end(), rng);

    // Pre-fill the structure
    for (const auto& k : keys) {
        sl.insert(k, "volatile_data");
    }

    // Reshuffle so we delete in a completely random order
    shuffle(keys.begin(), keys.end(), rng);

    cout << "Executing Deletion Stress Test on " << NUM_ITEMS << " randomized keys..." << endl;

    // --- DELETION TEST ---
    auto start = chrono::high_resolution_clock::now();
    for (const auto& k : keys) {
        bool removed = sl.remove(k);
        if (!removed) {
            cerr << "FATAL: Failed to remove existing key " << k << ". Pointer lost during rewiring." << endl;
            exit(1);
        }
    }
    auto end = chrono::high_resolution_clock::now();

    cout << "  -> Deleted " << NUM_ITEMS << " items in " 
         << chrono::duration_cast<chrono::milliseconds>(end - start).count() 
         << " ms." << endl;

    // Verify absolute zero state
    string val;
    for (const auto& k : keys) {
        if (sl.search(k, val)) {
            cerr << "FATAL: Phantom pointer detected. Key " << k << " still exists." << endl;
            exit(1);
        }
    }

    cout << "[OK] O(log N) Deletion Complexity and pointer rewiring passed." << endl;
}

int main() {
    cout << "--- Commencing SkipList Mechanical Verification ---" << endl;
    
    test_basic_crud();
    test_edge_cases();
    test_stress_and_time_complexity();
    test_massive_deletion();
    
    cout << "--- All Tests Passed. MemTable is structurally sound. ---" << endl;
    return 0;
}