cmake_minimum_required(VERSION 3.17)
project(StrataKV)

# Enforce C++17 for std::filesystem, std::shared_mutex, and std::make_unique
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# We must link POSIX threads because DB uses shared_mutex
find_package(Threads REQUIRED)

# 1. Compile the core database engine into a static library
add_library(lsm_engine
    src/skiplist.cpp
    src/wal.cpp
    src/sstable.cpp
    src/db.cpp
)
target_link_libraries(lsm_engine PRIVATE Threads::Threads)

# 2. Compile the CLI tool and link it to the engine
add_executable(strata_cli tools/cli.cpp)
target_link_libraries(strata_cli PRIVATE lsm_engine)

# 3. Compile the benchmarking tool
add_executable(db_bench tools/db_bench.cpp)
target_link_libraries(db_bench PRIVATE lsm_engine)