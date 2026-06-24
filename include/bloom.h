#pragma once
#include <string>
#include <vector>
#include <cstdint>

class BloomFilter {
private:
    std::vector<uint8_t> bit_array; 
    uint32_t num_bits;
    uint32_t num_hashes;

    // Generates N hash values for a given key
    std::vector<uint32_t> hash(const std::string& key) const;

public:
    // Initialize an empty filter
    BloomFilter(uint32_t expected_items, double false_positive_rate = 0.01);
    
    // Initialize from existing raw byte data (for loading from disk)
    BloomFilter(const std::vector<uint8_t>& raw_data, uint32_t bits, uint32_t hashes);

    void add(const std::string& key);
    bool possibly_contains(const std::string& key) const;

    // Getters for serialization to SSTable
    const std::vector<uint8_t>& get_raw_data() const;
    uint32_t get_num_bits() const;
    uint32_t get_num_hashes() const;
};