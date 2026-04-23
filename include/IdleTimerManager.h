#pragma once

#include "HttpConnection.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class IdleTimerManager {
public:
    explicit IdleTimerManager(int idleTimeoutMs);

    void schedule(const std::shared_ptr<HttpConnection>& connection);
    int nextPollTimeoutMs() const;
    std::vector<std::shared_ptr<HttpConnection>> collectExpired() const;

private:
    struct TimerNode {
        int64_t expireAtMs;
        int fd;
        int64_t activityAtMs;
        std::weak_ptr<HttpConnection> connection;

        bool operator<(const TimerNode& other) const {
            return expireAtMs > other.expireAtMs;
        }
    };

    static int64_t nowMs();

    int idleTimeoutMs_;
    mutable std::mutex mutex_;
    mutable std::priority_queue<TimerNode> heap_;
};
