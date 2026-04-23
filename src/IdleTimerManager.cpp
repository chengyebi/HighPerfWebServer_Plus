#include "IdleTimerManager.h"

#include <algorithm>
#include <chrono>

IdleTimerManager::IdleTimerManager(int idleTimeoutMs)
    : idleTimeoutMs_(idleTimeoutMs) {}

void IdleTimerManager::schedule(const std::shared_ptr<HttpConnection>& connection) {
    if (!connection || idleTimeoutMs_ <= 0 || connection->isClosed()) {
        return;
    }
    const int64_t activityAtMs = connection->lastActiveMs();
    std::lock_guard<std::mutex> lock(mutex_);
    heap_.push(TimerNode{
        activityAtMs + idleTimeoutMs_,
        connection->getFd(),
        activityAtMs,
        connection
    });
}

int IdleTimerManager::nextPollTimeoutMs() const {
    if (idleTimeoutMs_ <= 0) {
        return 100;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = nowMs();
    while (!heap_.empty()) {
        const auto& top = heap_.top();
        auto connection = top.connection.lock();
        if (!connection || connection->isClosed() || connection->lastActiveMs() != top.activityAtMs) {
            heap_.pop();
            continue;
        }
        const int64_t waitMs = top.expireAtMs - now;
        if (waitMs <= 0) {
            return 0;
        }
        return static_cast<int>(std::min<int64_t>(waitMs, 100));
    }
    return 100;
}

std::vector<std::shared_ptr<HttpConnection>> IdleTimerManager::collectExpired() const {
    std::vector<std::shared_ptr<HttpConnection>> expired;
    if (idleTimeoutMs_ <= 0) {
        return expired;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = nowMs();
    while (!heap_.empty()) {
        const auto& top = heap_.top();
        auto connection = top.connection.lock();
        if (!connection || connection->isClosed() || connection->lastActiveMs() != top.activityAtMs) {
            heap_.pop();
            continue;
        }
        if (top.expireAtMs > now) {
            break;
        }
        expired.push_back(connection);
        heap_.pop();
    }
    return expired;
}

int64_t IdleTimerManager::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
