#include "../include/skiplist.h"
#include <random>
#include <utility>

using namespace std;
 
// Node Constructor Implementation
Node::Node(string k, string v, int size)
    : key(move(k)), value(move(v)), next(size, nullptr) {}; 


int Skiplist:: randomLevel() {
    static random_device rd;  
    static mt19937 gen(rd()); 
    uniform_real_distribution<float> dist(0.0f, 1.0f);

    int lvl = 0;
    while (dist(gen) < probability && lvl < max_level) {
        lvl++;
    }
    return lvl;
}

Skiplist::Skiplist(int max_lvl, float prob)
    : max_level(max_lvl), probability(prob), current_level(0) {
    head = new Node("", "", max_level + 1);
}

Skiplist::~Skiplist(){
    while(head != nullptr){
        Node*h = head; 
        head = h -> next[0]; 
        delete h; 
    }
}

bool Skiplist::search(const string& key, string& out_value){
    Node* h = head; 

    // perform similar to binary jumping
    for(int lvl = current_level; lvl >= 0; lvl--){
        while(h -> next[lvl] != nullptr && h -> next[lvl] -> key < key){
            h = h -> next[lvl]; 
        }
    }

    h = h-> next[0]; 
    if (h != nullptr && h -> key == key){
        out_value = h -> value; 
        return true; 
    }
    return false; 
}; 




void Skiplist::insert(const string& key, const string& value){
    Node* h = head; 

    // Pre-allocate the update array to align index with level.
    // update[i] will store the pointer to the node that precedes the insertion spot at level i.
    vector<Node*> update(1 + max_level, nullptr); 

    for(int lvl = current_level; lvl >= 0; lvl--){
        while(h -> next[lvl] != nullptr && h -> next[lvl] -> key < key){
            h = h -> next[lvl]; 
        }
        update[lvl] = h; 
    }
    // h is the exact point where new node is inserted. 
    h = h-> next[0]; 

    // update a key if it already exists; 
    if (h != nullptr && h -> key == key){
        h -> value = value; 
        return ; 
    }

    int new_lvl = randomLevel(); 
    if (new_lvl > current_level){
        for(int i = 1+current_level; i <= new_lvl ; i++){
            // fix the higher levels to head. 
            update[i] = head; 
        }
        current_level = new_lvl; 
    }

    Node* new_node = new Node(key, value, new_lvl+1); 
    for(int i = 0; i <= new_lvl; i++){
        new_node -> next[i] = update[i] -> next[i]; 
        update[i] -> next[i] = new_node; 
    }
};



bool Skiplist::remove(const string& key){
    Node* h = head; 

    // Pre-allocate the update array to align index with level.
    // update[i] will store the pointer to the node that precedes the insertion spot at level i.
    vector<Node*> update(1 + max_level, nullptr); 

    for(int lvl = current_level; lvl >= 0; lvl--){
        while(h -> next[lvl] != nullptr && h -> next[lvl] -> key < key){
            h = h -> next[lvl]; 
        }
        update[lvl] = h; 
    }

    // h is the exact point of the node to be removed. 
    h = h-> next[0]; 
    if (h == nullptr || h->key != key) {
        return false; 
    }

    for(int i = 0; i <= current_level; i++){
        if (update[i]-> next[i] != h){
            break; 
        }
        update[i] -> next[i] = h -> next[i]; 
    }

    delete h; 

    while (current_level > 0 && head->next[current_level] == nullptr) {
        current_level--;
    }

    return true; 
};