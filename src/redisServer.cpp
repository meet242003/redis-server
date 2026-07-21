#include "../include/redisServer.h"
#include "../include/redisCommandHandler.h" 
#include "../include/redisDatabase.h"
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <iostream>
#include <thread>
#include <cstring>
#include <vector>
#include <signal.h>

RedisServer *globalServer = nullptr;

void signalHandler(int signum) {
    if(globalServer) {
        std::cout<<"Caught signal "<< signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}

void RedisServer::setupSignalHandler() {
    signal(SIGINT, signalHandler);
}

RedisServer::RedisServer(int port) : port(port), server_socket(-1), running(false) {
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::shutdown() {
    if(server_socket!=-1) {
        if (!RedisDatabase::getInstance().dump("dump.my_rdb")) {
            std::cerr<<"Error dumping the database\n";
        } else {
            std::cout<<"Database dumped in file dump.my_rdb\n";
        }
        close(server_socket);
    }
    running = false;
}

void RedisServer::run() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cout << "socket creation failed\n";
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cout << "bind failed\n";
        return;
    }
    
    if (listen(server_socket, 10) < 0) {
        std::cout << "listen failed\n";
        return;
    }
    
    running = true;
    std::cout << "Server running on port " << port << "\n";

    std::vector<std::thread> workerThreads;
    RedisCommandHandler handler;

    while(running) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if(client_socket < 0) {
            if(running) {
                std::cerr<<"Error accepting client connection\n";
            }
            break;
        }

        workerThreads.emplace_back([client_socket, &handler](){
            char buffer[1024];
            while(true) {
                memset(buffer, 0, sizeof(buffer));
                int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                if(bytes<=0) break;
                std::string request(buffer, bytes);
                std::string response = handler.processCommand(request);
                send(client_socket, response.c_str(), response.size(), 0);
            }
            close(client_socket);
        });
    }

    for(auto &t : workerThreads) {
        if(t.joinable()) t.join();
    }

    //shutdown
    if (!RedisDatabase::getInstance().dump("dump.my_rdb")) {
        std::cerr<<"Error dumping the database\n";
    } else {
        std::cout<<"Database dumped in file dump.my_rdb\n";
    }
}