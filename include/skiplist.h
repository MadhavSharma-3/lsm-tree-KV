#pragma once
#include <string>
#include <vector>

// dont do this :0
// using namespace std; 

// The foundational block of your MemTable.
struct Node {
    std::string key;
    std::string value;
    
    std::vector<Node*> next;

    Node(std::string k, std::string v, int size);
};

class Skiplist {
private:
    int max_level;
    float probability;
    int current_level;
    
    
    int randomLevel();
    
public:
    Node* head;
    
    Skiplist(int max_lvl = 16, float prob = 0.5f);
    ~Skiplist(); 

    void insert(const std::string& key, const std::string& value);
    bool search(const std::string& key, std::string& out_value);
    bool remove(const std::string& key);
};