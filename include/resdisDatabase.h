#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

class RedisDatabase {
    public:
        static RedisDatabase& getInstance();
        bool dump(const std::string& filename);
        bool load(const std::string& filename);

        bool flushAll();

        void set(const std::string& key, const std::string& value);
        bool get(const std::string& key, const std::string& value);
        std::vector<std::string> keys();
        std::string type(const std::string& key);
        bool del(const std::string& key);
    private:
        RedisDatabase() = default;
        ~RedisDatabase() = default;
        RedisDatabase(const RedisDatabase&) = delete;
        RedisDatabase& operator=(const RedisDatabase&) = delete;
        std::mutex db_mutex;
        std::unordered_map<std::string, std::string>kv_store;
        std::unordered_map<std::string, std::vector<std::string>>list_store;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>hash_store;
}

#endif