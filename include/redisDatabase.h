#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <utility>
#include <chrono>

struct SortedSet {
    std::unordered_map<std::string, double> member_scores;
    std::set<std::pair<double, std::string>> score_members;
};

class RedisDatabase {
    public:
        static RedisDatabase& getInstance();
        bool dump(const std::string& filename);
        bool load(const std::string& filename);

        // key - value operations
        bool flushAll();
        void set(const std::string& key, const std::string& value);
        bool get(const std::string& key, std::string& value);
        bool incr(const std::string& key, long long& result);
        bool decr(const std::string& key, long long& result);
        bool incrby(const std::string& key, long long increment, long long& result);
        bool decrby(const std::string& key, long long decrement, long long& result);
        std::vector<std::string> keys();
        std::string type(const std::string& key);
        bool del(const std::string& key);
        bool expire(const std::string& key, int seconds);
        int ttl(const std::string& key);
        bool rename(const std::string& oldKey, const std::string& newKey);

        // list operations
        std::vector<std::string> lget(const std::string& key);
        ssize_t llen(const std::string& key);
        void lpush(const std::string& key, const std::string& value);
        void rpush(const std::string& key, const std::string& value);
        bool lpop(const std::string& key, std::string& value);
        bool rpop(const std::string& key, std::string& value);
        int lrem(const std::string& key, int count, const std::string& value);
        bool lindex(const std::string& key, int index, std::string& value);
        bool lset(const std::string& key, int index, const std::string& value);

        // hash operations
        bool hset(const std::string& key, const std::string& field, const std::string& value);
        bool hget(const std::string& key, const std::string& field, std::string& value);
        bool hexists(const std::string& key, const std::string& field);
        bool hdel(const std::string& key, const std::string& field);
        std::unordered_map<std::string, std::string> hgetall(const std::string& key);
        std::vector<std::string> hkeys(const std::string& key);
        std::vector<std::string> hvals(const std::string& key);
        ssize_t hlen(const std::string& key);
        bool hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& fieldValues);

        // set operations
        int sadd(const std::string& key, const std::vector<std::string>& members);
        int srem(const std::string& key, const std::vector<std::string>& members);
        bool sismember(const std::string& key, const std::string& member);
        std::vector<std::string> smembers(const std::string& key);
        size_t scard(const std::string& key);

        // sorted set operations
        int zadd(const std::string& key, const std::vector<std::pair<double, std::string>>& scoreMembers);
        int zrem(const std::string& key, const std::vector<std::string>& members);
        std::vector<std::string> zrange(const std::string& key, int start, int stop, bool withScores);
        bool zscore(const std::string& key, const std::string& member, double& score);
        size_t zcard(const std::string& key);

    private:
        bool checkAndRemoveIfExpired(const std::string& key);
        RedisDatabase() = default;
        ~RedisDatabase() = default;
        RedisDatabase(const RedisDatabase&) = delete;
        RedisDatabase& operator=(const RedisDatabase&) = delete;
        std::mutex db_mutex;
        std::unordered_map<std::string, std::string>kv_store;
        std::unordered_map<std::string, std::vector<std::string>>list_store;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>hash_store;
        std::unordered_map<std::string, std::unordered_set<std::string>>set_store;
        std::unordered_map<std::string, SortedSet>zset_store;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry_map;
};
#endif