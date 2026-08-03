#include "../include/redisCommandHandler.h"
#include "../include/redisDatabase.h"
#include <vector>
#include <sstream>
#include <algorithm>

// RESP parser:
// *2\r\n$4\r\nPING\r\n$4\r\nTEST\r\n
// *2 -> array has two elements
// $4 -> length of the following string
// PING
// TEST
std::vector<std::string> parseRespCommand(const std::string &input) {
    std::vector<std::string> tokens;

    if(input.empty()) return tokens;

    // if input do not contain * fallback to whitespace as separator.
    if(input[0]!='*') {
        std::istringstream iss(input);
        std::string token;
        while(iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    // handle array
    size_t pos = 0;
    if(input[pos] != '*') return tokens;
    pos++;

    size_t crlf_pos = input.find("\r\n", pos);
    if(crlf_pos == std::string::npos) return tokens; // Invalid 

    int num_elements = std::stoi(input.substr(pos, crlf_pos - pos));
    pos = crlf_pos + 2;

    try {
        for(int i = 0; i < num_elements; ++i) {
            if(pos >= input.length() || input[pos] != '$') break; // Invalid 
            pos++;
            crlf_pos = input.find("\r\n", pos);
            if(crlf_pos == std::string::npos) break; // Invalid 
            int string_len = std::stoi(input.substr(pos, crlf_pos - pos));
            pos = crlf_pos + 2;
            if(pos + string_len + 2 > input.length()) break; //Invalid
            std::string token = input.substr(pos, string_len);
            tokens.push_back(token);
            pos += string_len + 2;
        }
    } catch (const std::exception&) {
        // Return parsed tokens so far if exception occurred
    }
    return tokens;
}

RedisCommandHandler::RedisCommandHandler() {

}

std::string RedisCommandHandler::processCommand(const std::string& commandLine) {
    // Use RESP parser
    auto tokens = parseRespCommand(commandLine);
    if (tokens.empty()) return "-Error: Empty command\r\n";

    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    std::ostringstream response;
    // connect to database
    RedisDatabase& db = RedisDatabase::getInstance();

    if (cmd == "PING") {
        response << "+PONG\r\n";
    } else if (cmd == "ECHO") {
        if(tokens.size() < 2) {
            response << "-Error: ECHO requires a message\r\n";
        } else {
            response << "+" << tokens[1] << "\r\n";
        }
    } else if (cmd == "FLUSHALL") {
        db.flushAll();
        response << "+OK\r\n";
    } 

    // key/value operations

    else if(cmd == "SET") {
        if ( tokens.size() < 3 ) {
            response << "-Error: SET requires key and value\r\n";
        } else {
            db.set(tokens[1], tokens[2]);
            response << "+OK\r\n";
        }
    } else if(cmd == "GET") {
        std::string value;
        if(tokens.size() < 2) {
            response << "-Error: GET requires key\r\n";
        } else {
            if(db.get(tokens[1], value)) {
                response << "$" << value.size() << "\r\n" << value << "\r\n";
            } else {
                response << "$-1\r\n";
            }
        }
    } else if (cmd == "INCR") {
        if (tokens.size() < 2) {
            response << "-ERROR: INCR requires a key\r\n";
        } else {
            long long result = 0;
            if (db.incr(tokens[1], result)) {
                response << ":" << std::to_string(result) << "\r\n";
            } else {
                response << "-ERROR: value is not an integer or out of range\r\n";
            }
        }
    } else if (cmd == "DECR") {
        if (tokens.size() < 2) {
            response << "-ERROR: DECR requires a key\r\n";
        } else {
            long long result = 0;
            if (db.decr(tokens[1], result)) {
                response << ":" << std::to_string(result) << "\r\n";
            } else {
                response << "-ERROR: value is not an integer or out of range\r\n";
            }
        }
    } else if (cmd == "INCRBY") {
        if (tokens.size() < 3) {
            response << "-ERROR: INCRBY requires a key and an increment\r\n";
        } else {
            try {
                long long inc = std::stoll(tokens[2]);
                long long result = 0;
                if (db.incrby(tokens[1], inc, result)) {
                    response << ":" << std::to_string(result) << "\r\n";
                } else {
                    response << "-ERROR: value is not an integer or out of range\r\n";
                }
            } catch (const std::exception&) {
                response << "-ERROR: value is not an integer or out of range\r\n";
            }
        }
    } else if (cmd == "DECRBY") {
        if (tokens.size() < 3) {
            response << "-ERROR: DECRBY requires a key and a decrement\r\n";
        } else {
            try {
                long long dec = std::stoll(tokens[2]);
                long long result = 0;
                if (db.decrby(tokens[1], dec, result)) {
                    response << ":" << std::to_string(result) << "\r\n";
                } else {
                    response << "-ERROR: value is not an integer or out of range\r\n";
                }
            } catch (const std::exception&) {
                response << "-ERROR: value is not an integer or out of range\r\n";
            }
        }
    } else if(cmd == "KEYS") {
        std::vector<std::string> keys = db.keys();
        response << "*" << keys.size() << "\r\n";
        for(auto &key : keys) {
            response << "$" << key.size() << "\r\n" << key << "\r\n";
        }
    } else if(cmd == "TYPE") {
        if(tokens.size() < 2) {
            response << "-ERROR: TYPE requires a key\r\n";
        } else {
            response << "+" << db.type(tokens[1]) <<"\r\n";
        }
    } else if(cmd == "DEL" || cmd == "UNLINK") {
        if(tokens.size() < 2) {
            response << "-ERROR: " << cmd << " requires a key\r\n";
        } else {
            bool res = db.del(tokens[1]);
            response << ":" << (res ? 1 : 0) << "\r\n";
        }
    } else if(cmd == "EXPIRE") {
        if(tokens.size() < 3) {
            response << "-ERROR: EXPIRE requires a key and time in seconds\r\n";
        } else {
            int seconds = std::stoi(tokens[2]);
            if (db.expire(tokens[1], seconds)) {
                response << ":1\r\n";
            } else {
                response << ":0\r\n";
            }
        }
    } else if (cmd == "TTL") {
        if(tokens.size() < 2) {
            response << "-ERROR: TTL requires a key\r\n";
        } else {
            int rem = db.ttl(tokens[1]);
            response << ":" << std::to_string(rem) << "\r\n";
        }
    } else if (cmd == "RENAME") {
        if(tokens.size() < 3) {
            response << "-ERROR: RENAME requires a old key and new key\r\n";
        } else {
            if (db.rename(tokens[1], tokens[2]))
            {
                response << "+OK\r\n";
            }
        }
    } else if (cmd == "LGET") {
        if(tokens.size() < 2 ) {
            response << "-ERROR: LGET requires a key\r\n";
        } else {
            std::vector<std::string>vals =  db.lget(tokens[1]);
            response << "*" << vals.size() << "\r\n";
            for(auto &val : vals) {
                response << "$" << val.size() << "\r\n" << val << "\r\n";
            }
        }
    } else if (cmd == "LLEN") {
        if (tokens.size() < 2) {
            response << "-ERROR: LLEN requires a key\r\n"; 
        } else {
            ssize_t len = db.llen(tokens[1]);
            response << ":" << std::to_string(len) << "\r\n";
        }
    } else if (cmd == "LPUSH") {
        if(tokens.size() < 3) {
            response << "-ERROR: LPUSH requires key and value\r\n";
        } 
        for(size_t i = 2; i < tokens.size(); i++) {
            db.lpush(tokens[1], tokens[i]);
        }
        ssize_t len = db.llen(tokens[1]);
        response << ":" << std::to_string(len) << "\r\n";
    } else if (cmd == "RPUSH") {
        if(tokens.size() < 3) {
            response << "-ERROR: RPUSH requires key and value\r\n";
        } 
        for(size_t i = 2; i < tokens.size(); i++) {
            db.rpush(tokens[1], tokens[i]);
        }
        ssize_t len = db.llen(tokens[1]);
        response << ":" << std::to_string(len) << "\r\n";
    } else if (cmd == "LPOP") {
        if(tokens.size() < 2) {
            response << "-ERROR: LPOP requires a key\r\n";
        } else {
            std::string value;
            if(db.lpop(tokens[1], value)) {
                response << "$" << std::to_string(value.size()) << "\r\n" << value <<"\r\n";
            } else {
                response << "$-1\r\n";
            }
        }
    } else if (cmd == "RPOP") {
        if(tokens.size() < 2 ) {
            response << "-ERROR: RPOP requires a key\r\n";
        } else {
            std::string value;
            if(db.rpop(tokens[1], value)) {
                response << "$" << std::to_string(value.size()) << "\r\n" << value << "\r\n";
            } else {
                response << "$-1\r\n";
            }
        }
    } else if (cmd == "LREM") {
        if (tokens.size() < 4) 
            response << "-ERROR: LREM requires key, count and value\r\n";
        else {
            try {
                int count = std::stoi(tokens[2]);
                int removed = db.lrem(tokens[1], count, tokens[3]);
                response << ":" << std::to_string(removed) << "\r\n";
            } catch (const std::exception&) {
                response << "-ERROR: Invalid count\r\n";
            }
        }
    } else if (cmd == "LINDEX") {
        if (tokens.size() < 3) 
            response << "-ERROR: LINDEX requires key and index\r\n";
        else {
            try {
                int index = std::stoi(tokens[2]);
                std::string value;
                if (db.lindex(tokens[1], index, value)) 
                    response <<  "$" << std::to_string(value.size()) << "\r\n" << value << "\r\n";
                else 
                    response << "$-1\r\n";
            } catch (const std::exception&) {
                response << "-ERROR: Invalid index\r\n";
            }
        }
    } else if (cmd == "LSET") {
        if (tokens.size() < 4) 
            response << "-ERROR: LSET requires key, index and value\r\n";
        else {
            try {
                int index = std::stoi(tokens[2]);
                if (db.lset(tokens[1], index, tokens[3]))
                    response << "+OK\r\n";
                else 
                    response << "-ERROR: Index out of range\r\n";
            } catch (const std::exception&) {
                response << "-ERROR: Invalid index\r\n";
            }
        }
    } else if (cmd == "HSET") {
        if(tokens.size() < 4) {
            response << "-ERROR: HSET requires key, field and value\r\n";
        } else {
            db.hset(tokens[1], tokens[2], tokens[3]);
            response << ":1\r\n";
        }
    } else if (cmd == "HGET") {
        if(tokens.size() < 3) {
            response << "-ERROR: HGET requires key and field\r\n";
        } else {
            std::string value;
            if(db.hget(tokens[1], tokens[2], value)) {
                response << "$" << value.size() << "\r\n" <<  value << "\r\n";
            } else {
                response << "$-1\r\n";
            }
        }
    } else if (cmd == "HEXISTS")  {
        if(tokens.size() < 3) {
            response << "-ERROR: HEXISTS requires key and field\r\n";
        } else {
            bool exists = db.hexists(tokens[1], tokens[2]);
            response << ":" << std::to_string(exists ? 1 : 0) << "\r\n";
        }
    } else if (cmd == "HDEL") {
        if (tokens.size() < 3) {
            response << "-ERROR: HDEL requires key and field\r\n";
        } else {
            bool res = db.hdel(tokens[1], tokens[2]);
            response << ":" << std::to_string(res ? 1 : 0) << "\r\n";
        }
    } else if (cmd == "HGETALL") {
        if (tokens.size() < 2) {
            response << "-ERROR: HGETALL requires a key\r\n";
        } else {
            auto hash = db.hgetall(tokens[1]);
            response << "*" << hash.size() * 2 << "\r\n";
            for(const auto& pair : hash) {
                response << "$" << pair.first.size() << "\r\n" << pair.first << "\r\n";
                response << "$" << pair.second.size() << "\r\n" << pair.second << "\r\n";
            }
        }
    } else if (cmd == "HKEYS") {
        if(tokens.size() < 2) {
            response << "-ERROR: HKEYS requires a key\r\n";
        } else {
            auto keys = db.hkeys(tokens[1]);
            response << "*" << keys.size() << "\r\n";
            for(const auto& key : keys) {
                response << "$" << key.size() << "\r\n" << key << "\r\n";
            }
        }
    } else if (cmd == "HVALS") {
        if(tokens.size() < 2) {
            response << "-ERROR: HVALS requires a key\r\n";
        } else {
            auto vals = db.hvals(tokens[1]);
            response << "*" << vals.size() << "\r\n";
            for(const auto& val : vals) {
                response << "$" << val.size() << "\r\n" << val << "\r\n";
            }
        }
    } else if (cmd == "HLEN") {
        if(tokens.size() < 2) {
            response << "-ERROR: HLEN requires a key\r\n";
        } else {
            size_t len = db.hlen(tokens[1]);
            response << ":" << std::to_string(len) << "\r\n";
        }
    } else if (cmd == "HMSET") {
        if (tokens.size() < 4 || (tokens.size()%2 == 1)) {
            response << "-ERROR: HMSET requires a key followed by field value pairs\r\n";
        } else {
            std::vector<std::pair<std::string, std::string>> fieldValues;
            for(size_t i = 2; i < tokens.size(); i += 2) {
                fieldValues.emplace_back({tokens[i], tokens[i+1]});
            }
            db.hmset(tokens[1], fieldValues);
            response << "+OK\r\n";
        }
    } else if (cmd == "SADD") {
        if (tokens.size() < 3) {
            response << "-ERROR: SADD requires a key and at least one member\r\n";
        } else {
            std::vector<std::string> members(tokens.begin() + 2, tokens.end());
            int added = db.sadd(tokens[1], members);
            if (added == -1) {
                response << "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            } else {
                response << ":" << added << "\r\n";
            }
        }
    } else if (cmd == "SREM") {
        if (tokens.size() < 3) {
            response << "-ERROR: SREM requires a key and at least one member\r\n";
        } else {
            std::vector<std::string> members(tokens.begin() + 2, tokens.end());
            int removed = db.srem(tokens[1], members);
            if (removed == -1) {
                response << "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            } else {
                response << ":" << removed << "\r\n";
            }
        }
    } else if (cmd == "SISMEMBER") {
        if (tokens.size() < 3) {
            response << "-ERROR: SISMEMBER requires a key and a member\r\n";
        } else {
            bool isMember = db.sismember(tokens[1], tokens[2]);
            response << ":" << (isMember ? 1 : 0) << "\r\n";
        }
    } else if (cmd == "SMEMBERS") {
        if (tokens.size() < 2) {
            response << "-ERROR: SMEMBERS requires a key\r\n";
        } else {
            auto members = db.smembers(tokens[1]);
            response << "*" << members.size() << "\r\n";
            for (const auto& member : members) {
                response << "$" << member.size() << "\r\n" << member << "\r\n";
            }
        }
    } else if (cmd == "SCARD") {
        if (tokens.size() < 2) {
            response << "-ERROR: SCARD requires a key\r\n";
        } else {
            size_t card = db.scard(tokens[1]);
            response << ":" << card << "\r\n";
        }
    } else if (cmd == "ZADD") {
        if (tokens.size() < 4 || (tokens.size() - 2) % 2 != 0) {
            response << "-ERROR: ZADD requires a key and at least one score member pair\r\n";
        } else {
            std::vector<std::pair<double, std::string>> scoreMembers;
            try {
                for (size_t i = 2; i < tokens.size(); i += 2) {
                    double score = std::stod(tokens[i]);
                    scoreMembers.emplace_back(score, tokens[i + 1]);
                }
                int added = db.zadd(tokens[1], scoreMembers);
                if (added == -1) {
                    response << "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
                } else {
                    response << ":" << added << "\r\n";
                }
            } catch (const std::exception&) {
                response << "-ERROR: value is not a valid float\r\n";
            }
        }
    } else if (cmd == "ZREM") {
        if (tokens.size() < 3) {
            response << "-ERROR: ZREM requires a key and at least one member\r\n";
        } else {
            std::vector<std::string> members(tokens.begin() + 2, tokens.end());
            int removed = db.zrem(tokens[1], members);
            if (removed == -1) {
                response << "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            } else {
                response << ":" << removed << "\r\n";
            }
        }
    } else if (cmd == "ZRANGE") {
        if (tokens.size() < 4) {
            response << "-ERROR: ZRANGE requires a key, start, and stop\r\n";
        } else {
            try {
                int start = std::stoi(tokens[2]);
                int stop = std::stoi(tokens[3]);
                bool withScores = false;
                if (tokens.size() >= 5) {
                    std::string opt = tokens[4];
                    for (char& c : opt) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    if (opt == "WITHSCORES") {
                        withScores = true;
                    }
                }
                auto range = db.zrange(tokens[1], start, stop, withScores);
                response << "*" << range.size() << "\r\n";
                for (const auto& item : range) {
                    response << "$" << item.size() << "\r\n" << item << "\r\n";
                }
            } catch (const std::exception&) {
                response << "-ERROR: value is not an integer or out of range\r\n";
            }
        }
    } else if (cmd == "ZSCORE") {
        if (tokens.size() < 3) {
            response << "-ERROR: ZSCORE requires a key and a member\r\n";
        } else {
            double score = 0.0;
            if (db.zscore(tokens[1], tokens[2], score)) {
                std::ostringstream oss;
                oss << score;
                std::string s = oss.str();
                response << "$" << s.size() << "\r\n" << s << "\r\n";
            } else {
                response << "$-1\r\n";
            }
        }
    } else if (cmd == "ZCARD") {
        if (tokens.size() < 2) {
            response << "-ERROR: ZCARD requires a key\r\n";
        } else {
            size_t card = db.zcard(tokens[1]);
            response << ":" << card << "\r\n";
        }
    } else {
        response << "-ERROR: Unknown command '" << cmd << "'\r\n";
    }

    return response.str();
}