#include "../include/redisPubSub.h"
#include <sstream>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

PubSubManager& PubSubManager::getInstance() {
    static PubSubManager instance;
    return instance;
}

int PubSubManager::subscribe(int client_fd, const std::string& channel) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    channel_subscribers[channel].insert(client_fd);
    client_channels[client_fd].insert(channel);
    return static_cast<int>(client_channels[client_fd].size());
}

int PubSubManager::unsubscribe(int client_fd, const std::string& channel) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    auto cit = channel_subscribers.find(channel);
    if (cit != channel_subscribers.end()) {
        cit->second.erase(client_fd);
        if (cit->second.empty()) {
            channel_subscribers.erase(cit);
        }
    }
    auto clit = client_channels.find(client_fd);
    if (clit != client_channels.end()) {
        clit->second.erase(channel);
        int rem = static_cast<int>(clit->second.size());
        if (clit->second.empty()) {
            client_channels.erase(clit);
        }
        return rem;
    }
    return 0;
}

std::vector<std::pair<std::string, int>> PubSubManager::unsubscribeAll(int client_fd) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    std::vector<std::pair<std::string, int>> result;
    auto clit = client_channels.find(client_fd);
    if (clit == client_channels.end()) {
        return result;
    }
    auto channels = clit->second;
    for (const auto& ch : channels) {
        auto cit = channel_subscribers.find(ch);
        if (cit != channel_subscribers.end()) {
            cit->second.erase(client_fd);
            if (cit->second.empty()) {
                channel_subscribers.erase(cit);
            }
        }
        clit->second.erase(ch);
        result.emplace_back(ch, static_cast<int>(clit->second.size()));
    }
    client_channels.erase(client_fd);
    return result;
}

bool PubSubManager::isSubscribed(int client_fd) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    auto it = client_channels.find(client_fd);
    return (it != client_channels.end() && !it->second.empty());
}

std::vector<std::string> PubSubManager::getClientChannels(int client_fd) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    std::vector<std::string> channels;
    auto it = client_channels.find(client_fd);
    if (it != client_channels.end()) {
        for (const auto& ch : it->second) {
            channels.push_back(ch);
        }
    }
    return channels;
}

int PubSubManager::publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(pubsub_mutex);
    auto cit = channel_subscribers.find(channel);
    if (cit == channel_subscribers.end()) {
        return 0;
    }

    std::ostringstream oss;
    oss << "*3\r\n"
        << "$7\r\nmessage\r\n"
        << "$" << channel.size() << "\r\n" << channel << "\r\n"
        << "$" << message.size() << "\r\n" << message << "\r\n";
    std::string resp = oss.str();

    int sent_count = 0;
    for (int fd : cit->second) {
#ifdef MSG_NOSIGNAL
        ssize_t bytes = send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
#else
        ssize_t bytes = send(fd, resp.c_str(), resp.size(), 0);
#endif
        if (bytes > 0) {
            sent_count++;
        }
    }
    return sent_count;
}
