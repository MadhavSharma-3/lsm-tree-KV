#include "../include/db.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <random>

using namespace std;

mutex cout_mutex;
atomic<int> global_puts(0);
atomic<int> global_gets(0);
atomic<int> global_dels(0);
atomic<int> get_hits(0);

// Telemetry tracker to prove the engine isn't deadlocked
atomic<int> total_completed(0);

void random_chaos_worker(DB* db, int thread_id, int ops, int max_key_space) {
    try {
        // Thread-local RNG prevents threads from locking each other while generating random numbers
        thread_local mt19937 generator(thread_id + chrono::system_clock::now().time_since_epoch().count());
        uniform_int_distribution<int> key_dist(1, max_key_space);
        uniform_int_distribution<int> op_dist(1, 100);

        int local_puts = 0, local_gets = 0, local_dels = 0, local_hits = 0;

        for (int i = 0; i < ops; i++) {
            // Pick a random key from the shared pool
            string key = "key_" + to_string(key_dist(generator));
            
            // Roll a D100 to determine the operation type
            int roll = op_dist(generator);

            if (roll <= 60) {
                // 60% chance: GET (Read Heavy)
                string val;
                if (db->get(key, val)) local_hits++;
                local_gets++;
            } 
            else if (roll <= 90) {
                // 30% chance: PUT (Write Heavy)
                string val = "random_payload_data_" + to_string(roll) + "_for_" + key;
                db->put(key, val);
                local_puts++;
            } 
            else {
                // 10% chance: DELETE (Garbage Creation)
                db->remove(key);
                local_dels++;
            }

            // The Telemetry Heartbeat
            int current_ops = ++total_completed;
            if (current_ops % 20000 == 0) {
                lock_guard<mutex> lock(cout_mutex);
                cout << "[Heartbeat] Engine processed " << current_ops << " operations..." << endl;
            }
        }

        // Batch atomic updates to avoid atomic lock contention in the tight loop
        global_puts += local_puts;
        global_gets += local_gets;
        global_dels += local_dels;
        get_hits += local_hits;

    } catch (const exception& e) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "\n[FATAL] Thread " << thread_id << " panicked: " << e.what() << endl;
    }
}

int main() {
    auto now = chrono::system_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    string db_dir = "./bench_data_" + to_string(ms);
    
    try {
        filesystem::create_directories(db_dir);
    } catch (const filesystem::filesystem_error& e) {
        cerr << "\n[FATAL] Failed to create new benchmark directory.\nSystem Error: " << e.what() << endl;
        return 1; 
    }

    // --- BENCHMARK CONFIGURATION ---
    // Scaled down to prevent 3-minute synchronous compaction stalls on Windows file I/O
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 25000;
    const int KEY_SPACE = 50000; // Dense collisions to force MemTable flushes and compaction
    const int TOTAL_OPS = NUM_THREADS * OPS_PER_THREAD;

    cout << "--- StrataKV Real-World Production Benchmark ---" << endl;
    cout << "Target Directory: " << db_dir << endl;
    cout << "Threads: " << NUM_THREADS << endl;
    cout << "Key Space (M): " << KEY_SPACE << " unique keys" << endl;
    cout << "Total Operations: " << TOTAL_OPS << "\n" << endl;

    DB* db_ptr = nullptr;
    try {
        db_ptr = new DB(db_dir);
    } catch (const exception& e) {
        cerr << "\n[FATAL] Engine failed to boot: " << e.what() << endl;
        return 1;
    }
    DB& db = *db_ptr;

    cout << "Executing randomized workload (60% GET, 30% PUT, 10% DEL)..." << endl;
    
    vector<thread> workers;
    auto start_time = chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_THREADS; i++) {
        workers.emplace_back(random_chaos_worker, &db, i, OPS_PER_THREAD, KEY_SPACE);
    }
    for (auto& t : workers) {
        t.join(); 
    }

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    
    cout << "\n--- Benchmark Results ---" << endl;
    cout << "Total Time: " << duration << " ms" << endl;
    cout << "PUTs executed: " << global_puts.load() << endl;
    cout << "GETs executed: " << global_gets.load() << " (Hits: " << get_hits.load() << ")" << endl;
    cout << "DELs executed: " << global_dels.load() << endl;
    cout << "\nTRUE THROUGHPUT: " << (TOTAL_OPS / (duration / 1000.0)) << " ops/sec" << endl;

    delete db_ptr;
    return 0;
}