#include "../include/db.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>

using namespace std;

mutex cout_mutex;
atomic<int> failed_verifications(0);

void chaos_worker(DB* db, int thread_id, int ops) {
    try {
        for (int i = 0; i < ops; i++) {
            string key = "key_" + to_string(thread_id) + "_" + to_string(i);
            string val_v1 = "payload_version_1_" + to_string(i);
            string val_v2 = "payload_version_2_" + to_string(i);
            string out;

            // 1. Initial PUT
            db->put(key, val_v1);

            // 2. Immediate GET (Read-after-Write verification)
            if (!db->get(key, out) || out != val_v1) {
                failed_verifications++;
            }

            // 3. OVERWRITE (Pointer and value reassignment)
            db->put(key, val_v2);

            // 4. DELETE (Tombstone injection)
            db->remove(key);

            // 5. Phantom GET (Tombstone masking verification)
            // If this returns true, the engine resurrected deleted data from the disk.
            if (db->get(key, out)) {
                failed_verifications++;
            }
        }
    } catch (const exception& e) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "\n[FATAL] Thread " << thread_id << " panicked: " << e.what() << endl;
    }
}

int main() {
    // Dynamically generate a unique directory using a timestamp to bypass Windows file locks
    auto now = chrono::system_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    string db_dir = "./bench_data_" + to_string(ms);
    
    try {
        filesystem::create_directories(db_dir);
    } catch (const filesystem::filesystem_error& e) {
        cerr << "\n[FATAL] Failed to create new benchmark directory.\n" 
             << "System Error: " << e.what() << endl;
        return 1; 
    }

    cout << "--- StrataKV Convoluted Correctness Benchmark ---" << endl;
    cout << "Target Directory: " << db_dir << endl;
    
    DB* db_ptr = nullptr;
    try {
        db_ptr = new DB(db_dir);
    } catch (const exception& e) {
        cerr << "\n[FATAL] Engine failed to boot: " << e.what() << endl;
        return 1;
    }
    
    DB& db = *db_ptr;

    const int NUM_THREADS = 8;
    const int KEYS_PER_THREAD = 5000;
    // 5 operations per key (Put, Get, Put, Del, Get)
    const int TOTAL_OPS = NUM_THREADS * KEYS_PER_THREAD * 5; 

    cout << "Threads: " << NUM_THREADS << endl;
    cout << "Keys per thread: " << KEYS_PER_THREAD << endl;
    cout << "Total Lock Operations: " << TOTAL_OPS << "\n" << endl;

    cout << "Executing chaotic interleaved workload..." << endl;
    vector<thread> workers;
    auto start_time = chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_THREADS; i++) {
        workers.emplace_back(chaos_worker, &db, i, KEYS_PER_THREAD);
    }
    for (auto& t : workers) {
        t.join(); 
    }

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    
    cout << "Done. " << TOTAL_OPS << " ops in " << duration << " ms." << endl;
    cout << "Throughput: " << (TOTAL_OPS / (duration / 1000.0)) << " ops/sec" << endl;

    if (failed_verifications.load() > 0) {
        cout << "\n[FAIL] Consistency Check Failed: " << failed_verifications.load() << " data anomalies detected." << endl;
    } else {
        cout << "\n[OK] Absolute Consistency Maintained. Tombstones and locks are mechanically sound." << endl;
    }

    delete db_ptr;
    return 0;
}