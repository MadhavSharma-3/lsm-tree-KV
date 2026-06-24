#include "../include/bloom.h"
#include <cmath>
#include <functional>


using namespace std;


BloomFilter::BloomFilter(uint32_t expected_items, double false_positive_rate) {
    expected_items = max(expected_items, 1u); 
    // Optimal bit array size: m = -(n * ln(p)) / (ln(2)^2)
    num_bits = static_cast<uint32_t>(ceil(-(expected_items * log(false_positive_rate)) / 0.480453013918201));
    
    // Optimal number of hash functions: k = (m / n) * ln(2)
    num_hashes = static_cast<uint32_t>(ceil((num_bits / static_cast<double>(expected_items)) * 0.693147180559945));
    
    // Ensure at least 1 hash and minimum bits
    if (num_hashes == 0) num_hashes = 1;
    if (num_bits == 0) num_bits = 8;

    uint32_t num_bytes = (num_bits + 7) / 8;
    bit_array.assign(num_bytes, 0);
}



BloomFilter::BloomFilter(const vector<uint8_t>& raw_data, uint32_t bits, uint32_t hashes)
    : bit_array(raw_data), num_bits(bits), num_hashes(hashes) {}



vector<uint32_t> BloomFilter::hash(const string& key) const {
    vector<uint32_t> hashes;
    hashes.reserve(num_hashes);

    // Using std::hash as a base, combined with linear probing for multiple hashes
    // In production, MurmurHash3 or CityHash is used, but this avoids external dependencies.
    size_t base_hash1 = std::hash<string>{}(key);
    size_t base_hash2 = std::hash<string>{}(key + "_salt");

    for (uint32_t i = 0; i < num_hashes; i++) {
        // Double-hashing technique: h_i(x) = (h1(x) + i * h2(x)) % m
        uint32_t combined_hash = static_cast<uint32_t>((base_hash1 + i * base_hash2) % num_bits);
        hashes.push_back(combined_hash);
    }
    return hashes;
}



void BloomFilter::add(const string& key) {
    vector<uint32_t> hashes = hash(key);
    // mark all the bits that hash into. 
    for (uint32_t h : hashes) {
        uint32_t byte_idx = h / 8;
        uint32_t bit_idx = h % 8;
        bit_array[byte_idx] |= (1 << bit_idx);
    }
}

// educational information  : 
// If you make an array of 1000 bools, you are burning 1000 bytes to store 1000 bits of information. That is a 800% memory waste.
// You might think std::vector<bool> fixes this. It doesn't. std::vector<bool> is notoriously broken in C++
//  because the standard library tries to auto-compress the bits, returning weird proxy objects instead 
//  of actual references, which destroys performance and prevents direct memory mapping.


bool BloomFilter::possibly_contains(const string& key) const {
    vector<uint32_t> hashes = hash(key);
    for (uint32_t h : hashes) {
        uint32_t byte_idx = h / 8;
        uint32_t bit_idx = h % 8;
        
        // If even a single mapped bit is 0, the key is mathematically guaranteed to not exist.
        if ((bit_array[byte_idx] & (1 << bit_idx)) == 0) {
            return false; 
        }
    }
    return true; 
}


const vector<uint8_t>& BloomFilter::get_raw_data() const {
    return bit_array;
}

uint32_t BloomFilter::get_num_bits() const {
    return num_bits;
}

uint32_t BloomFilter::get_num_hashes() const {
    return num_hashes;
}