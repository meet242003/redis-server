#include "../include/redisServer.h"
#include "../include/redisCommandHandler.h" 
#include "../include/redisDatabase.h"
#include "../include/redisPubSub.h"
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

static RedisServer* globalServer = nullptr;

void signalHandler(int signum) {
    if(globalServer) {
        std::cout<<"Caught signal "<< signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}

void RedisServer::setupSignalHandler() {
    signal(SIGINT, signalHandler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
}

RedisServer::RedisServer(int port) : port(port), server_socket(-1), running(false) {
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::shutdown() {
    if(server_socket != -1) {
        close(server_socket);
        server_socket = -1;
    }
    running = false;
}

void RedisServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool RedisServer::isCompleteCommand(const std::string& input, size_t& cmd_len) {
    if (input.empty() || input[0] != '*') return false;
    size_t pos = 0;
    size_t crlf_pos = input.find("\r\n", pos);
    if (crlf_pos == std::string::npos) return false;

    int num_elements = 0;
    try {
        num_elements = std::stoi(input.substr(pos + 1, crlf_pos - (pos + 1)));
    } catch (...) {
        return false;
    }

    pos = crlf_pos + 2;
    for (int i = 0; i < num_elements; ++i) {
        if (pos >= input.size() || input[pos] != '$') return false;
        crlf_pos = input.find("\r\n", pos);
        if (crlf_pos == std::string::npos) return false;

        int string_len = 0;
        try {
            string_len = std::stoi(input.substr(pos + 1, crlf_pos - (pos + 1)));
        } catch (...) {
            return false;
        }

        pos = crlf_pos + 2;
        if (pos + string_len + 2 > input.size()) return false;
        if (input.substr(pos + string_len, 2) != "\r\n") return false;
        pos += string_len + 2;
    }
    cmd_len = pos;
    return true;
}

void RedisServer::closeClient(size_t poll_index) {
    if (poll_index >= fds.size()) return;
    int fd = fds[poll_index].fd;
    PubSubManager::getInstance().unsubscribeAll(fd);
    close(fd);
    clients.erase(fd);
    fds.erase(fds.begin() + poll_index);
}

void RedisServer::handleNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd >= 0) {
        setNonBlocking(client_fd);
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        fds.push_back(pfd);
        clients[client_fd] = ClientContext{client_fd, ""};
    }
}

bool RedisServer::handleClientData(size_t poll_index, RedisCommandHandler& handler) {
    int client_fd = fds[poll_index].fd;
    char buffer[4096];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        closeClient(poll_index);
        return false;
    }
    auto& ctx = clients[client_fd];
    ctx.in_buffer.append(buffer, bytes);

    size_t cmd_len = 0;
    while (isCompleteCommand(ctx.in_buffer, cmd_len)) {
        std::string request = ctx.in_buffer.substr(0, cmd_len);
        ctx.in_buffer.erase(0, cmd_len);

        std::string response = handler.processCommand(request, client_fd);
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent <= 0) {
            closeClient(poll_index);
            return false;
        }
    }
    return true;
}

void RedisServer::run() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cout << "socket creation failed\n";
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(server_socket);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cout << "bind failed\n";
        return;
    }
    
    if (listen(server_socket, 128) < 0) {
        std::cout << "listen failed\n";
        return;
    }
    
    running = true;
    std::cout << "Server running on port " << port << " (Single-threaded Event Loop)\n";

    fds.clear();
    clients.clear();
    struct pollfd pfd;
    pfd.fd = server_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;
    fds.push_back(pfd);

    RedisCommandHandler handler;

    while (running) {
        int ret = poll(fds.data(), fds.size(), 100);
        if (ret < 0 && running) {
            std::cerr << "poll error\n";
            break;
        }

        for (size_t i = 0; i < fds.size(); ) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (i == 0) {
                    std::cerr << "Server socket error\n";
                    running = false;
                    break;
                } else {
                    closeClient(i);
                    continue;
                }
            }

            if (fds[i].revents & POLLIN) {
                if (i == 0) {
                    handleNewConnection();
                    ++i;
                } else {
                    bool still_open = handleClientData(i, handler);
                    if (still_open) {
                        ++i;
                    }
                }
            } else {
                ++i;
            }
        }
    }

    for (size_t i = 1; i < fds.size(); ++i) {
        close(fds[i].fd);
    }
    fds.clear();
    clients.clear();

    // shutdown
    if (!RedisDatabase::getInstance().dump("dump.my_rdb")) {
        std::cerr << "Error dumping the database\n";
    } else {
        std::cout << "Database dumped in file dump.my_rdb\n";
    }
}