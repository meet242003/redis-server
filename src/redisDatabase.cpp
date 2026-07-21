#include "../include/redisDatabase.h"
#include <fstream>
#include <sstream>

RedisDatabase& RedisDatabase::getInstance() {
    static RedisDatabase instance;
    return instance;
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

bool RedisDatabase::flushAll() {
    std::lock_guard<std::mutex> lock(db_mutex);
    kv_store.clear();
    list_store.clear();
    hash_store.clear();
    return true;
}

void RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    kv_store[key] = value;
}

bool RedisDatabase::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = kv_store.find(key);
    if(it!=kv_store.end()) {
        value = it->second;
        return true;
    } 
    return false;
}

std::vector<std::string> RedisDatabase::keys() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<std::string> result;
    for(auto &pair : kv_store) {
        result.push_back(pair.first);
    }
    for(auto &pair : list_store) {
        result.push_back(pair.first);
    }
    for(auto &pair : hash_store) {
        result.push_back(pair.first);
    }
    return result;
}

std::string RedisDatabase::type(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if(kv_store.find(key)!=kv_store.end()) {
        return "string";
    } else if (list_store.find(key)!=list_store.end()) {
        return "list";
    } else if (hash_store.find(key)!=hash_store.end()) {
        return "hash";
    }
    return "none";
}

bool RedisDatabase::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    bool erased = false;
    erased |= kv_store.erase(key) > 0;
    erased |= list_store.erase(key) > 0;
    erased |= hash_store.erase(key) > 0;
    return erased;
}

bool RedisDatabase::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    bool exists = (kv_store.find(key)!=kv_store.end()) || (list_store.find(key)!=list_store.end()) || (hash_store.find(key)!=hash_store.end());
    if(!exists) return false;
    
    expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return true;
}

bool RedisDatabase::rename(const std::string& oldKey, const std::string& newKey) {
    std::lock_guard<std::mutex> lock(db_mutex);

    bool found = false;
    
    auto itKv = kv_store.find(oldKey);
    if(itKv != kv_store.end()) {
        kv_store[newKey] = itKv->second;
        kv_store.erase(oldKey);
        found=true;
    }

    auto itList = list_store.find(oldKey);
    if(itList != list_store.end()) {
        list_store[newKey] = itList->second;
        list_store.erase(oldKey);
        found=true;
    }

    auto itHash = hash_store.find(oldKey);
    if(itHash != hash_store.end()) {
        hash_store[newKey] = itHash->second;
        hash_store.erase(oldKey);
        found=true;
    }

    auto itExpire = expiry_map.find(oldKey);
    if(itExpire != expiry_map.end()) {
        expiry_map[newKey] = itExpire->second;
        expiry_map.erase(oldKey);
    }

    return found;
}

std::vector<std::string> RedisDatabase::lget(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if(it != list_store.end()) {
        return it->second;
    } 
    return {};
}

ssize_t RedisDatabase::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if(it != list_store.end()) {
        return it->second.size();
    }
    return 0;
}

void RedisDatabase::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    list_store[key].insert(list_store[key].begin(), value);
}

void RedisDatabase::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    list_store[key].push_back(value);
}

bool RedisDatabase::lpop(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if(it != list_store.end() && !it->second.empty()) {
        value = it->second.front();
        it->second.erase(it->second.begin());
        return true;
    }
    return false;
}

bool RedisDatabase::rpop(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if(it != list_store.end() && !it->second.empty()) {
        value = it->second.back();
        it->second.pop_back();
        return true;
    }
    return false;
}


int RedisDatabase::lrem(const std::string& key, int count, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    int removed = 0;
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return 0;

    auto& lst = it->second;

    if (count == 0) {
        // Remove all occurances
        auto new_end = std::remove(lst.begin(), lst.end(), value);
        removed = std::distance(new_end, lst.end());
        lst.erase(new_end, lst.end());
    } else if (count > 0) {
        // Remove from head to tail
        for (auto iter = lst.begin(); iter != lst.end() && removed < count; ) {
            if (*iter == value) {
                iter = lst.erase(iter);
                ++removed;
            } else {
                ++iter;
            }
        }
    } else {
        // Remove from tail to head (count is negative)
        for (auto riter = lst.rbegin(); riter != lst.rend() && removed < (-count); ) {
            if (*riter == value) {
                auto fwdIter = riter.base();
                --fwdIter;
                fwdIter = lst.erase(fwdIter);
                ++removed;
                riter = std::reverse_iterator<std::vector<std::string>::iterator>(fwdIter);
            } else {
                ++riter;
            }
        }
    }
    return removed;
}

bool RedisDatabase::lindex(const std::string& key, int index, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return false;

    const auto& lst = it->second;
    if (index < 0)
        index = lst.size() + index;
    if (index < 0 || index >= static_cast<int>(lst.size()))
        return false;
    
    value = lst[index];
    return true;
}

bool RedisDatabase::lset(const std::string& key, int index, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) 
        return false;

    auto& lst = it->second;
    if (index < 0)
        index = lst.size() + index;
    if (index < 0 || index >= static_cast<int>(lst.size()))
        return false;
    
    lst[index] = value;
    return true;
}
