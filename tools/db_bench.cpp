#include "../include/db.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <exception>

using namespace std;

// Atomic counters for multi-threaded telemetry
atomic<int> completed_ops(0);
atomic<int> read_hits(0);

// Mutex to prevent console output from garbling during a concurrent crash
mutex console_mutex;

void print_metrics(const string& name, int total_ops, double duration_ms) {
    double seconds = duration_ms / 1000.0;
    double throughput = total_ops / seconds;
    lock_guard<mutex> lock(console_mutex);
    cout << "--- " << name << " ---" << endl;
    cout << "Time: " << duration_ms << " ms" << endl;
    cout << "Throughput: " << throughput << " ops/sec" << endl;
    cout << "---------------------------------" << endl;
}

// Workload Generator Thread
void ycsb_worker(DB* db, int thread_id, int ops, int max_keys, double read_ratio) {
    try {
        // Thread-local RNG
        thread_local mt19937 gen(thread_id + chrono::system_clock::now().time_since_epoch().count());
        uniform_int_distribution<int> key_dist(0, max_keys - 1);
        uniform_real_distribution<double> op_dist(0.0, 1.0);

        string out_val;
        int local_hits = 0;

        for (int i = 0; i < ops; i++) {
            string key = "user_" + to_string(key_dist(gen));
            
            if (op_dist(gen) < read_ratio) {
                // READ
                if (db->get(key, out_val)) {
                    local_hits++;
                }
            } else {
                // WRITE
                db->put(key, "updated_payload_data_" + to_string(thread_id));
            }
        }
        read_hits += local_hits;

    } catch (const exception& e) {
        lock_guard<mutex> lock(console_mutex);
        cerr << "\n[FATAL] Thread " << thread_id << " threw an exception: " << e.what() << endl;
    } catch (...) {
        lock_guard<mutex> lock(console_mutex);
        cerr << "\n[FATAL] Thread " << thread_id << " threw an UNKNOWN exception." << endl;
    }
}

int main() {
    string db_dir = "./embedded_ycsb_data";
    
    // 1. Filesystem Guard
    try {
        if (filesystem::exists(db_dir)) {
            filesystem::remove_all(db_dir); // Start completely fresh
        }
        filesystem::create_directories(db_dir);
    } catch (const filesystem::filesystem_error& e) {
        cerr << "\n[FATAL] Filesystem Error during setup. Is a file handle still open?" << endl;
        cerr << "Details: " << e.what() << endl;
        return 1;
    }

    cout << "===========================================" << endl;
    cout << " StrataKV: Embedded YCSB Benchmark Suite" << endl;
    cout << "===========================================" << endl;

    // 2. Engine Boot Guard
    DB* db = nullptr;
    try {
        db = new DB(db_dir);
    } catch (const exception& e) {
        cerr << "\n[FATAL] Engine failed to boot: " << e.what() << endl;
        return 1;
    }

    const int SEED_RECORDS = 200000;
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 25000;
    const int TOTAL_OPS = NUM_THREADS * OPS_PER_THREAD;

    // --- PHASE 0: BULK LOAD ---
    cout << "\n[*] Phase 0: Bulk Loading " << SEED_RECORDS << " records..." << endl;
    auto start = chrono::high_resolution_clock::now();
    try {
        for (int i = 0; i < SEED_RECORDS; i++) {
            db->put("user_" + to_string(i), "initial_payload_data_block");
        }
    } catch (const exception& e) {
        cerr << "\n[FATAL] Bulk Load failed: " << e.what() << endl;
        delete db;
        return 1;
    }
    auto end = chrono::high_resolution_clock::now();
    print_metrics("Bulk Load (100% Sequential Write)", SEED_RECORDS, chrono::duration_cast<chrono::milliseconds>(end - start).count());

    // --- WORKLOAD A: UPDATE HEAVY (50% Read, 50% Write) ---
    cout << "\n[*] Executing Workload A (50/50 Mixed)..." << endl;
    vector<thread> threads;
    start = chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(ycsb_worker, db, i, OPS_PER_THREAD, SEED_RECORDS, 0.50);
    }
    for (auto& t : threads) t.join();
    end = chrono::high_resolution_clock::now();
    print_metrics("Workload A", TOTAL_OPS, chrono::duration_cast<chrono::milliseconds>(end - start).count());

    // --- WORKLOAD B: READ MOST (95% Read, 5% Write) ---
    cout << "\n[*] Executing Workload B (95/5 Read-Heavy)..." << endl;
    threads.clear();
    start = chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(ycsb_worker, db, i, OPS_PER_THREAD, SEED_RECORDS, 0.95);
    }
    for (auto& t : threads) t.join();
    end = chrono::high_resolution_clock::now();
    print_metrics("Workload B", TOTAL_OPS, chrono::duration_cast<chrono::milliseconds>(end - start).count());

    // --- WORKLOAD C: READ ONLY (100% Read) ---
    cout << "\n[*] Executing Workload C (100% Read)..." << endl;
    threads.clear();
    start = chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(ycsb_worker, db, i, OPS_PER_THREAD, SEED_RECORDS, 1.0);
    }
    for (auto& t : threads) t.join();
    end = chrono::high_resolution_clock::now();
    print_metrics("Workload C", TOTAL_OPS, chrono::duration_cast<chrono::milliseconds>(end - start).count());

    delete db;
    return 0;
}



// Comparison (YCSB Zipfian)...



// ===========================================

//  TARGET: StrataKV (Custom Engine)

// ===========================================

// [*] Phase 0: Bulk Loading 200000 records...

// --- Bulk Load (100% Sequential Write) ---

// Time: 6193 ms

// Throughput: 32294.5 ops/sec

// ---------------------------------



// [*] Executing Workload A (50/50 Mixed - Zipfian)...



// [Compaction] Merging 4 fragmented SSTables. 

// [Compaction] Complete. Deleted keys dropped. Disk space reclaimed.

// --- Workload A (50/50 Mixed - Zipfian) ---

// Time: 4423 ms

// Throughput: 45218.2 ops/sec

// ---------------------------------



// [*] Executing Workload B (95/5 Read-Heavy - Zipfian)...

// --- Workload B (95/5 Read-Heavy - Zipfian) ---

// Time: 1228 ms

// Throughput: 162866 ops/sec

// ---------------------------------



// [*] Executing Workload C (100% Read - Zipfian)...

// --- Workload C (100% Read - Zipfian) ---

// Time: 564 ms

// Throughput: 354610 ops/sec

// ---------------------------------



// ===========================================

//  TARGET: LevelDB (Google)

// ===========================================

// [*] Phase 0: Bulk Loading 200000 records...

// --- Bulk Load (100% Sequential Write) ---

// Time: 2527 ms

// Throughput: 79145.2 ops/sec

// ---------------------------------



// [*] Executing Workload A (50/50 Mixed - Zipfian)...

// --- Workload A (50/50 Mixed - Zipfian) ---

// Time: 2034 ms

// Throughput: 98328.4 ops/sec

// ---------------------------------



// [*] Executing Workload B (95/5 Read-Heavy - Zipfian)...

// --- Workload B (95/5 Read-Heavy - Zipfian) ---

// Time: 619 ms

// Throughput: 323102 ops/sec

// ---------------------------------



// [*] Executing Workload C (100% Read - Zipfian)...

// --- Workload C (100% Read - Zipfian) ---

// Time: 634 ms

// Throughput: 315457 ops/sec

// ---------------------------------









// Initializing Embedded C++ Architecture Comparison (Uniform Random)...

// ===========================================
//  TARGET: StrataKV (Custom Engine)
// ===========================================
// [*] Phase 0: Bulk Loading 200000 records...
// --- Bulk Load (100% Sequential Write) ---
// Time: 6682 ms
// Throughput: 29931.2 ops/sec
// ---------------------------------

// [*] Executing Workload A (50/50 Mixed - Uniform)...

// [Compaction] Merging 4 fragmented SSTables. 
// [Compaction] Complete. Deleted keys dropped. Disk space reclaimed.
// --- Workload A (50/50 Mixed - Uniform) ---
// Time: 6900 ms
// Throughput: 28985.5 ops/sec
// ---------------------------------

// [*] Executing Workload B (95/5 Read-Heavy - Uniform)...
// --- Workload B (95/5 Read-Heavy - Uniform) ---
// Time: 4327 ms
// Throughput: 46221.4 ops/sec
// ---------------------------------

// [*] Executing Workload C (100% Read - Uniform)...
// --- Workload C (100% Read - Uniform) ---
// Time: 3756 ms
// Throughput: 53248.1 ops/sec
// ---------------------------------

// ===========================================
//  TARGET: LevelDB (Google)
// ===========================================
// [*] Phase 0: Bulk Loading 200000 records...
// --- Bulk Load (100% Sequential Write) ---
// Time: 2597 ms
// Throughput: 77011.9 ops/sec
// ---------------------------------

// [*] Executing Workload A (50/50 Mixed - Uniform)...
// --- Workload A (50/50 Mixed - Uniform) ---
// Time: 1961 ms
// Throughput: 101989 ops/sec
// ---------------------------------

// [*] Executing Workload B (95/5 Read-Heavy - Uniform)...
// --- Workload B (95/5 Read-Heavy - Uniform) ---
// Time: 508 ms
// Throughput: 393701 ops/sec
// ---------------------------------

// [*] Executing Workload C (100% Read - Uniform)...
// --- Workload C (100% Read - Uniform) ---
// Time: 276 ms
// Throughput: 724638 ops/sec
// ---------------------------------