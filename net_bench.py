import socket
import threading
import time
import statistics
import sqlite3
import os
import random

# --- Configuration ---
SEED_RECORDS = 100000    # Massive initial dataset to bloat the disk
NUM_THREADS = 1
OPS_PER_THREAD = 45000    # 40,000 total interleaved operations
HOST = '127.0.0.1'
PORT = 8080
DB_PATH = "mixed_baseline.db"

latencies_stratakv = []
latencies_sqlite = []
lock = threading.Lock()

def seed_stratakv():
    print(f"[*] Seeding StrataKV with {SEED_RECORDS} records (Forcing massive disk fragmentation)...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    
    batch = ""
    for i in range(SEED_RECORDS):
        batch += f"PUT user_{i} initial_payload_data_block\n"
        if i % 2000 == 0:
            s.sendall(batch.encode('utf-8'))
            _ = s.recv(4096 * 10) 
            batch = ""
            
    if batch:
        s.sendall(batch.encode('utf-8'))
        _ = s.recv(4096 * 10)
    s.close()

def seed_sqlite():
    print(f"[*] Seeding SQLite3 with {SEED_RECORDS} records...")
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
        
    conn = sqlite3.connect(DB_PATH)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.execute("CREATE TABLE kvstore (key TEXT PRIMARY KEY, value TEXT)")
    
    cursor = conn.cursor()
    dummy_data = [(f"user_{i}", "initial_payload_data_block") for i in range(SEED_RECORDS)]
    cursor.executemany("INSERT INTO kvstore VALUES (?, ?)", dummy_data)
    conn.commit()
    conn.close()

def stratakv_mixed_worker(thread_id):
    local_latencies = []
    # Thread-local RNG to avoid Python GIL lock contention
    rng = random.Random(thread_id + time.time()) 
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        
        buffer = ""
        for _ in range(OPS_PER_THREAD):
            # Target a random existing key to force index lookups and overwrites
            target_key = f"user_{rng.randint(0, SEED_RECORDS - 1)}"
            
            # 50/50 Read/Write Split
            if rng.random() < 0.5:
                command = f"GET {target_key}\n"
            else:
                command = f"PUT {target_key} updated_payload_data_{thread_id}\n"
                
            cmd_bytes = command.encode('utf-8')
            
            start_req = time.perf_counter()
            s.sendall(cmd_bytes)
            
            while "\n" not in buffer:
                chunk = s.recv(1024).decode('utf-8')
                if not chunk: break
                buffer += chunk
                
            end_req = time.perf_counter()
            
            if "\n" in buffer:
                buffer = buffer.split("\n", 1)[1]
                
            local_latencies.append((end_req - start_req) * 1000)
            
        s.close()
    except Exception as e:
        print(f"StrataKV Thread {thread_id} failed: {e}")
        
    with lock:
        latencies_stratakv.extend(local_latencies)

def sqlite_mixed_worker(thread_id):
    local_latencies = []
    rng = random.Random(thread_id + time.time())
    
    try:
        conn = sqlite3.connect(DB_PATH, timeout=60.0)
        conn.execute("PRAGMA busy_timeout = 60000")
        cursor = conn.cursor()
        
        for _ in range(OPS_PER_THREAD):
            target_key = f"user_{rng.randint(0, SEED_RECORDS - 1)}"
            
            start_req = time.perf_counter()
            
            if rng.random() < 0.5:
                cursor.execute("SELECT value FROM kvstore WHERE key = ?", (target_key,))
                _ = cursor.fetchone()
            else:
                cursor.execute("INSERT OR REPLACE INTO kvstore (key, value) VALUES (?, ?)", (target_key, f"updated_payload_data_{thread_id}"))
                conn.commit() 
                
            end_req = time.perf_counter()
            local_latencies.append((end_req - start_req) * 1000)
            
        conn.close()
    except Exception as e:
        print(f"SQLite Thread {thread_id} failed: {e}")
        
    with lock:
        latencies_sqlite.extend(local_latencies)

def print_metrics(name, duration, latencies, total_ops):
    if not latencies:
        print(f"{name} failed to collect telemetry.")
        return
        
    latencies.sort()
    avg_lat = statistics.mean(latencies)
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    throughput = total_ops / duration
    
    print(f"\n--- {name} Results ---")
    print(f"Throughput: {throughput:.2f} ops/sec")
    print(f"Avg Latency: {avg_lat:.2f} ms")
    print(f"p50 Latency: {p50:.2f} ms")
    print(f"p95 Latency: {p95:.2f} ms")
    print(f"p99 Latency: {p99:.2f} ms")

if __name__ == "__main__":
    print("===========================================")
    print(" STRATAKV vs SQLITE: 50/50 MIXED WORKLOAD  ")
    print("===========================================")
    
    seed_stratakv()
    seed_sqlite()
    
    TOTAL_OPS = NUM_THREADS * OPS_PER_THREAD
    print(f"\n[*] Firing {TOTAL_OPS} chaotic, interleaved GET and PUT operations...")
    
    print("\n[1/2] Benchmarking StrataKV (TCP)...")
    threads = []
    start_time = time.time()
    for i in range(NUM_THREADS):
        t = threading.Thread(target=stratakv_mixed_worker, args=(i,))
        threads.append(t)
        t.start()
    for t in threads: t.join()
    strata_duration = time.time() - start_time
    
    print("\n[2/2] Benchmarking SQLite3 (Disk B-Tree)...")
    threads = []
    start_time = time.time()
    for i in range(NUM_THREADS):
        t = threading.Thread(target=sqlite_mixed_worker, args=(i,))
        threads.append(t)
        t.start()
    for t in threads: t.join()
    sqlite_duration = time.time() - start_time
    
    print("\n===========================================")
    print_metrics("StrataKV (LSM-Tree)", strata_duration, latencies_stratakv, TOTAL_OPS)
    print_metrics("SQLite3 (B-Tree)", sqlite_duration, latencies_sqlite, TOTAL_OPS)
    print("===========================================")
    
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
        for ext in ['-wal', '-journal', '-shm']:
            if os.path.exists(DB_PATH + ext): os.remove(DB_PATH + ext)




# ===========================================

# --- StrataKV (LSM-Tree) Results ---
# Throughput: 10097.03 ops/sec
# Avg Latency: 0.78 ms
# p50 Latency: 0.61 ms
# p95 Latency: 1.75 ms
# p99 Latency: 4.13 ms

# --- SQLite3 (B-Tree) Results ---
# Throughput: 1677.96 ops/sec
# Avg Latency: 3.89 ms
# p50 Latency: 0.15 ms
# p95 Latency: 1.30 ms
# p99 Latency: 2.10 ms
# ===========================================

