#pragma once

#include "Epoll.h"
#include "HttpConnection.h"
#include "IdleTimerManager.h"
#include "ServerConfig.h"
#include "ServerLogger.h"
#include "ServerMetrics.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

class SubReactor {
public:
    SubReactor(size_t index,
               const ServerConfig& config,
               std::shared_ptr<ServerMetrics> metrics,
               std::shared_ptr<ServerLogger> logger);
    ~SubReactor();

    SubReactor(const SubReactor&) = delete;
    SubReactor& operator=(const SubReactor&) = delete;

    void start();
    void stop();
    void enqueueConnection(int clientFd);

private:
    void run();
    void processPendingConnections();
    void processExpiredConnections();
    void handleWakeup();
    void wakeup();

    size_t index_;
    ServerConfig config_;
    std::shared_ptr<ServerMetrics> metrics_;
    std::shared_ptr<ServerLogger> logger_;
    Epoll epoll_;
    IdleTimerManager timerManager_;
    int wakeFd_;
    std::mutex pendingMutex_;
    std::queue<int> pendingConnections_;
    std::unordered_map<int, std::shared_ptr<HttpConnection>> connections_;
    std::atomic<bool> running_;
    std::thread thread_;
};
