#include "../include/redisDatabase.h"
#include <fstream>
#include <sstream>

RedisDatabase& RedisDatabase::getInstance() {
    static RedisDatabase instance;
    return instance;
}

bool RedisDatabase::checkAndRemoveIfExpired(const std::string& key) {
    auto it = expiry_map.find(key);
    if (it == expiry_map.end()) {
        return false;
    }
    if (std::chrono::steady_clock::now() >= it->second) {
        kv_store.erase(key);
        list_store.erase(key);
        hash_store.erase(key);
        set_store.erase(key);
        zset_store.erase(key);
        expiry_map.erase(it);
        return true;
    }
    return false;
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

    for(auto &kv : set_store) {
        ofs<<"S "<<kv.first;
        for(auto &member : kv.second) {
            ofs<<" "<<member;
        }
        ofs<<"\n";
    }

    for(auto &kv : zset_store) {
        ofs<<"Z "<<kv.first;
        for(auto &member_score : kv.second.member_scores) {
            ofs<<" "<<member_score.first<<":"<<member_score.second;
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
    set_store.clear();
    expiry_map.clear();

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
                    std::string temp_val = hash_item.substr(separator_pos + 1);
                    hash_val[temp_key] = temp_val;
                }
            }
            hash_store[key] = hash_val;
        } else if (type == 'S') {
            std::string key;
            iss >> key;
            std::string item;
            std::unordered_set<std::string> set_val;
            while(iss >> item) {
                set_val.insert(item);
            }
            set_store[key] = set_val;
        } else if (type == 'Z') {
            std::string key;
            iss >> key;
            std::string item;
            SortedSet zset_val;
            while (iss >> item) {
                size_t separator_pos = item.find(':');
                if (separator_pos != std::string::npos) {
                    std::string member = item.substr(0, separator_pos);
                    double score = std::stod(item.substr(separator_pos + 1));
                    zset_val.member_scores[member] = score;
                    zset_val.score_members.insert({score, member});
                }
            }
            zset_store[key] = zset_val;
        }
    }

    return true;
}

bool RedisDatabase::flushAll() {
    std::lock_guard<std::mutex> lock(db_mutex);
    kv_store.clear();
    list_store.clear();
    hash_store.clear();
    set_store.clear();
    zset_store.clear();
    expiry_map.clear();
    return true;
}

void RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    kv_store[key] = value;
    expiry_map.erase(key);
}

bool RedisDatabase::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (checkAndRemoveIfExpired(key)) return false;
    auto it = kv_store.find(key);
    if(it!=kv_store.end()) {
        value = it->second;
        return true;
    } 
    return false;
}

bool RedisDatabase::incrby(const std::string& key, long long increment, long long& result) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);

    if (list_store.find(key) != list_store.end() || hash_store.find(key) != hash_store.end()) {
        return false;
    }

    long long current_val = 0;
    auto it = kv_store.find(key);
    if (it != kv_store.end()) {
        try {
            current_val = std::stoll(it->second);
        } catch (const std::exception&) {
            return false;
        }
    }

    result = current_val + increment;
    kv_store[key] = std::to_string(result);
    return true;
}

bool RedisDatabase::incr(const std::string& key, long long& result) {
    return incrby(key, 1, result);
}

bool RedisDatabase::decr(const std::string& key, long long& result) {
    return incrby(key, -1, result);
}

bool RedisDatabase::decrby(const std::string& key, long long decrement, long long& result) {
    return incrby(key, -decrement, result);
}

std::vector<std::string> RedisDatabase::keys() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<std::string> candidates;
    for(auto &pair : kv_store) {
        candidates.push_back(pair.first);
    }
    for(auto &pair : list_store) {
        candidates.push_back(pair.first);
    }
    for(auto &pair : hash_store) {
        candidates.push_back(pair.first);
    }
    for(auto &pair : set_store) {
        candidates.push_back(pair.first);
    }
    for(auto &pair : zset_store) {
        candidates.push_back(pair.first);
    }
    std::vector<std::string> result;
    for(auto &key : candidates) {
        if (!checkAndRemoveIfExpired(key)) {
            result.push_back(key);
        }
    }
    return result;
}

std::string RedisDatabase::type(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (checkAndRemoveIfExpired(key)) return "none";
    if(kv_store.find(key)!=kv_store.end()) {
        return "string";
    } else if (list_store.find(key)!=list_store.end()) {
        return "list";
    } else if (hash_store.find(key)!=hash_store.end()) {
        return "hash";
    } else if (set_store.find(key)!=set_store.end()) {
        return "set";
    } else if (zset_store.find(key)!=zset_store.end()) {
        return "zset";
    }
    return "none";
}

bool RedisDatabase::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    bool erased = false;
    erased |= kv_store.erase(key) > 0;
    erased |= list_store.erase(key) > 0;
    erased |= hash_store.erase(key) > 0;
    erased |= set_store.erase(key) > 0;
    erased |= zset_store.erase(key) > 0;
    expiry_map.erase(key);
    return erased;
}

bool RedisDatabase::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (checkAndRemoveIfExpired(key)) return false;
    bool exists = (kv_store.find(key)!=kv_store.end()) ||
                  (list_store.find(key)!=list_store.end()) ||
                  (hash_store.find(key)!=hash_store.end()) ||
                  (set_store.find(key)!=set_store.end()) ||
                  (zset_store.find(key)!=zset_store.end());
    if(!exists) return false;
    
    expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return true;
}

int RedisDatabase::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (checkAndRemoveIfExpired(key)) {
        return -2;
    }
    bool exists = (kv_store.find(key) != kv_store.end()) ||
                  (list_store.find(key) != list_store.end()) ||
                  (hash_store.find(key) != hash_store.end()) ||
                  (set_store.find(key) != set_store.end()) ||
                  (zset_store.find(key) != zset_store.end());
    if (!exists) {
        return -2;
    }
    auto it = expiry_map.find(key);
    if (it == expiry_map.end()) {
        return -1;
    }
    auto now = std::chrono::steady_clock::now();
    auto rem = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
    return static_cast<int>(rem < 0 ? 0 : rem);
}

bool RedisDatabase::rename(const std::string& oldKey, const std::string& newKey) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (checkAndRemoveIfExpired(oldKey)) return false;

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

    auto itSet = set_store.find(oldKey);
    if(itSet != set_store.end()) {
        set_store[newKey] = itSet->second;
        set_store.erase(oldKey);
        found=true;
    }

    auto itZset = zset_store.find(oldKey);
    if(itZset != zset_store.end()) {
        zset_store[newKey] = itZset->second;
        zset_store.erase(oldKey);
        found=true;
    }

    auto itExpire = expiry_map.find(oldKey);
    if(itExpire != expiry_map.end()) {
        expiry_map[newKey] = itExpire->second;
        expiry_map.erase(oldKey);
    } else {
        expiry_map.erase(newKey);
    }

    return found;
}

std::vector<std::string> RedisDatabase::lget(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = list_store.find(key);
    if(it != list_store.end()) {
        return it->second;
    } 
    return {};
}

ssize_t RedisDatabase::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = list_store.find(key);
    if(it != list_store.end()) {
        return it->second.size();
    }
    return 0;
}

void RedisDatabase::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    list_store[key].insert(list_store[key].begin(), value);
}

void RedisDatabase::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    list_store[key].push_back(value);
}

bool RedisDatabase::lpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = list_store.find(key);
    if(it != list_store.end() && !it->second.empty()) {
        value = it->second.front();
        it->second.erase(it->second.begin());
        return true;
    }
    return false;
}

bool RedisDatabase::rpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
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
    checkAndRemoveIfExpired(key);
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
    checkAndRemoveIfExpired(key);
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
    checkAndRemoveIfExpired(key);
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

bool RedisDatabase::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    hash_store[key][field] = value;
    return true;
}

bool RedisDatabase::hget(const std::string& key, const std::string& field, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = hash_store.find(key);
    if(it != hash_store.end()) {
        auto it2 = it->second.find(field);
        if(it2 != it->second.end()) {
            value = it2->second;
            return true;
        }
    }
    return false;
}

bool RedisDatabase::hexists(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = hash_store.find(key);
    if(it != hash_store.end()) {
        return it->second.find(field) != it->second.end();
    }
    return false;
}

bool RedisDatabase::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = hash_store.find(key);
    if(it != hash_store.end()) {
        return it->second.erase(field) > 0;
    }
    return false;
}

std::unordered_map<std::string, std::string> RedisDatabase::hgetall(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    if (hash_store.find(key) != hash_store.end())
        return hash_store[key];
    return {};
}

std::vector<std::string> RedisDatabase::hkeys(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    std::vector<std::string> fields;
    auto it = hash_store.find(key);
    if(it != hash_store.end()) {
        for(const auto& pair : it->second) {
            fields.emplace_back(pair.first);
        }
    }
    return fields;
}

std::vector<std::string> RedisDatabase::hvals(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    std::vector<std::string> values;
    auto it = hash_store.find(key);
    if(it != hash_store.end()) {
        for(const auto& pair : it->second) {
            values.emplace_back(pair.second);
        }
    }
    return values;
}

size_t RedisDatabase::hlen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = hash_store.find(key);
    return (it != hash_store.end()) ? it->second.size() : 0; 
}

bool RedisDatabase::hmset(const std::string&key, const std::vector<std::pair<std::string, std::string>>& fieldValues) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    for (const auto& pair: fieldValues) {
        hash_store[key][pair.first] = pair.second;
    }
    return true;
}

int RedisDatabase::sadd(const std::string& key, const std::vector<std::string>& members) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    if (kv_store.find(key) != kv_store.end() ||
        list_store.find(key) != list_store.end() ||
        hash_store.find(key) != hash_store.end()) {
        return -1; // WRONGTYPE
    }
    int added = 0;
    auto& s = set_store[key];
    for (const auto& member : members) {
        if (s.insert(member).second) {
            added++;
        }
    }
    return added;
}

int RedisDatabase::srem(const std::string& key, const std::vector<std::string>& members) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    if (kv_store.find(key) != kv_store.end() ||
        list_store.find(key) != list_store.end() ||
        hash_store.find(key) != hash_store.end()) {
        return -1; // WRONGTYPE
    }
    auto it = set_store.find(key);
    if (it == set_store.end()) {
        return 0;
    }
    int removed = 0;
    for (const auto& member : members) {
        removed += it->second.erase(member);
    }
    if (it->second.empty()) {
        set_store.erase(it);
    }
    return removed;
}

bool RedisDatabase::sismember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = set_store.find(key);
    if (it == set_store.end()) {
        return false;
    }
    return it->second.find(member) != it->second.end();
}

std::vector<std::string> RedisDatabase::smembers(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    std::vector<std::string> result;
    auto it = set_store.find(key);
    if (it != set_store.end()) {
        for (const auto& member : it->second) {
            result.push_back(member);
        }
    }
    return result;
}

size_t RedisDatabase::scard(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = set_store.find(key);
    if (it != set_store.end()) {
        return it->second.size();
    }
    return 0;
}

static std::string formatScore(double score) {
    std::ostringstream oss;
    oss << score;
    return oss.str();
}

int RedisDatabase::zadd(const std::string& key, const std::vector<std::pair<double, std::string>>& scoreMembers) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    if (kv_store.find(key) != kv_store.end() ||
        list_store.find(key) != list_store.end() ||
        hash_store.find(key) != hash_store.end() ||
        set_store.find(key) != set_store.end()) {
        return -1; // WRONGTYPE
    }
    int added = 0;
    auto& zs = zset_store[key];
    for (const auto& pair : scoreMembers) {
        double score = pair.first;
        const std::string& member = pair.second;
        auto it = zs.member_scores.find(member);
        if (it != zs.member_scores.end()) {
            double old_score = it->second;
            if (old_score != score) {
                zs.score_members.erase({old_score, member});
                zs.score_members.insert({score, member});
                it->second = score;
            }
        } else {
            zs.member_scores[member] = score;
            zs.score_members.insert({score, member});
            added++;
        }
    }
    return added;
}

int RedisDatabase::zrem(const std::string& key, const std::vector<std::string>& members) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    if (kv_store.find(key) != kv_store.end() ||
        list_store.find(key) != list_store.end() ||
        hash_store.find(key) != hash_store.end() ||
        set_store.find(key) != set_store.end()) {
        return -1; // WRONGTYPE
    }
    auto it = zset_store.find(key);
    if (it == zset_store.end()) {
        return 0;
    }
    int removed = 0;
    for (const auto& member : members) {
        auto mit = it->second.member_scores.find(member);
        if (mit != it->second.member_scores.end()) {
            double score = mit->second;
            it->second.score_members.erase({score, member});
            it->second.member_scores.erase(mit);
            removed++;
        }
    }
    if (it->second.member_scores.empty()) {
        zset_store.erase(it);
    }
    return removed;
}

std::vector<std::string> RedisDatabase::zrange(const std::string& key, int start, int stop, bool withScores) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    std::vector<std::string> result;
    auto it = zset_store.find(key);
    if (it == zset_store.end()) {
        return result;
    }
    int len = static_cast<int>(it->second.score_members.size());
    if (start < 0) start += len;
    if (stop < 0) stop += len;
    if (start < 0) start = 0;
    if (stop < 0) stop = 0;
    if (start >= len || start > stop) {
        return result;
    }
    if (stop >= len) stop = len - 1;

    auto sit = it->second.score_members.begin();
    std::advance(sit, start);
    for (int i = start; i <= stop; ++i, ++sit) {
        result.push_back(sit->second);
        if (withScores) {
            result.push_back(formatScore(sit->first));
        }
    }
    return result;
}

bool RedisDatabase::zscore(const std::string& key, const std::string& member, double& score) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = zset_store.find(key);
    if (it == zset_store.end()) {
        return false;
    }
    auto mit = it->second.member_scores.find(member);
    if (mit == it->second.member_scores.end()) {
        return false;
    }
    score = mit->second;
    return true;
}

size_t RedisDatabase::zcard(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    checkAndRemoveIfExpired(key);
    auto it = zset_store.find(key);
    if (it != zset_store.end()) {
        return it->second.member_scores.size();
    }
    return 0;
}

