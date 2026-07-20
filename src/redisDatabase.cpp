#include "../include/redisDatabase.h"
#include <fstream>

RedisDatabase& RedisDatabase::getInstance() {
    static RedisDatabase instance;
    return &instance;
}


bool RedisDatabase::dump(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ofstream ofs(filename, std::ios::binary);
    if(!ofs) return false;

    for(auto &kv : kv_store) {
        ofs<<"K "<<kv.first<<" "<<kv.second<<"\n";
    }

    for(auto &kv: list_store) {
        ofs<<"L "<<kv.first;
        for(auto &li : kv.second) {
            ofs<<" "<<li;
        }
        ofs<<"\n";
    }

    for(auto &kv : hash_store) {
        ofs<<"H "<<kv.first;
        for(auto &hv : kv.second) {
            ofs<<" "<<hv.first<<":"<<hv.second;
        }
        ofs<<"\n";
    }
    return true;
}

bool RedisDatabase::load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ifstream ifs(filename, std::ios::binary);
    if(!ifs) return false;

    kv_store.clear();
    list_store.clear();
    hash_store.clear();

    std::string line;

    while(std::getline(ifs, line)) {
        std::istringstream iss(line);
        char type;
        iss >> type;
        if(type == 'K') {
            std::string key, value;
            iss >> key >> value;
            kv_store[key] = value;
        } else if (type == 'L') {
            std::string key;
            iss >> key;
            std::string item;
            std::vector<std::string>list_val;
            while(iss >> item) {
                list_val.emplace_back(item);
            }
            list_store[key] = list_val;
        } else if (type == 'H') {
            std::string key;
            iss >> key;
            std::string hash_item;
            std::unordered_map<std::string, std::string>hash_val;
            while(iss >> hash_item) {
                size_t separator_pos = hash_item.find(":");
                if(separator_pos != std::string::npos) {
                    std::string temp_key = hash_item.substr(0, separator_pos);
                    std::string temp_val = hash_item.substr(separator_pos);
                    hash_val[temp_key] = temp_val;
                }
            }
            hash_store[key] = hash_val;
        }
    }

    return true;
}
