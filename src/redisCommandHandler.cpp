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

    for(int i = 0; i < num_elements; ++i) {
        if(pos >= input.length() || input[pos]) break; // Invalid 
        pos++;
        crlf_pos = input.find("\r\n", pos);
        if(crlf_pos == std::string::npos) break; // Invalid 
        int string_len = std::stoi(input.substr(pos, crlf_pos - pos));
        pos = crlf_pos + 2;
        if(pos + string_len >= input.length()) break; //Invalid
        std::string token = input.substr(pos, string_len);
        tokens.push_back(token);
        pos += string_len + 2;
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
    RedisDatabase& db = RedisDatabase.getInstance();

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
    }
    
    return response.str();
}