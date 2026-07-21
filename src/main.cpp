#include "../include/redisServer.h"
#include "../include/redisDatabase.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>


int main(int argc, char *argv[]) {
    int port = 6379; //default
    if(argc >= 2) port = std::stoi(argv[1]);
    RedisServer server(port);

    if(RedisDatabase::getInstance().load("dump.my_rdb")) {
        std::cout << "Database loaded from dump.my_rdb\n";
    } else {
        std::cout << "No dump found or failed; starting with empty database\n";
    }

    std::thread persistanceThread([](){
        while(true) {
            std::this_thread::sleep_for(std::chrono::seconds(300));
            if (!RedisDatabase::getInstance().dump("dump.my_rdb")) {
                std::cerr<<"Error dumping the database\n";
            } else {
                std::cout<<"Database dumped in file dump.my_rdb\n";
            }
        }
    });

    persistanceThread.detach();

    server.run();

    return 0;
}

