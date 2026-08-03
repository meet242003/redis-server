#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <atomic>
#include <string>
#include <vector>
#include <unordered_map>
#include <poll.h>

struct ClientContext {
    int fd;
    std::string in_buffer;
};

class RedisCommandHandler;

class RedisServer {
    public:
        RedisServer(int port);
        void run();
        void shutdown();
    private:
        int port;
        int server_socket;
        std::atomic<bool> running;
        std::vector<struct pollfd> fds;
        std::unordered_map<int, ClientContext> clients;

        void setupSignalHandler();
        void setNonBlocking(int fd);
        void handleNewConnection();
        bool handleClientData(size_t poll_index, RedisCommandHandler& handler);
        void closeClient(size_t poll_index);
        bool isCompleteCommand(const std::string& input, size_t& cmd_len);
};

#endif