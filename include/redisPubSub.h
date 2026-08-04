#ifndef REDIS_PUBSUB_H
#define REDIS_PUBSUB_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <mutex>

class PubSubManager {
public:
    static PubSubManager& getInstance();

    // Subscribe a client_fd to a channel. Returns number of channels client is subscribed to.
    int subscribe(int client_fd, const std::string& channel);

    // Unsubscribe a client_fd from a channel. Returns number of channels client is subscribed to.
    int unsubscribe(int client_fd, const std::string& channel);

    // Unsubscribe client_fd from all channels (used on disconnect or UNSUBSCRIBE with no args).
    // Returns a vector of pairs: (channel_name, remaining_count_after_unsub).
    std::vector<std::pair<std::string, int>> unsubscribeAll(int client_fd);

    // Check if client_fd is currently subscribed to any channels.
    bool isSubscribed(int client_fd);

    // Get all channels client_fd is subscribed to.
    std::vector<std::string> getClientChannels(int client_fd);

    // Publish a message to all clients subscribed to a channel.
    // Returns the number of clients that received the message.
    int publish(const std::string& channel, const std::string& message);

private:
    PubSubManager() = default;
    ~PubSubManager() = default;
    PubSubManager(const PubSubManager&) = delete;
    PubSubManager& operator=(const PubSubManager&) = delete;

    std::mutex pubsub_mutex;
    std::unordered_map<std::string, std::unordered_set<int>> channel_subscribers; // channel -> set of client fds
    std::unordered_map<int, std::unordered_set<std::string>> client_channels;     // client fd -> set of channels
};

#endif
