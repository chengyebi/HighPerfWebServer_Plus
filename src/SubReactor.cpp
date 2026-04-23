#include "SubReactor.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {
constexpr uint64_t kWakeSignal = 1;
}

SubReactor::SubReactor(size_t index,
                       const ServerConfig& config,
                       std::shared_ptr<ServerMetrics> metrics,
                       std::shared_ptr<ServerLogger> logger)
    : index_(index),
      config_(config),
      metrics_(std::move(metrics)),
      logger_(std::move(logger)),
      epoll_(),
      timerManager_(config_.idleTimeoutMs),
      wakeFd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      running_(false) {
    if (wakeFd_ == -1) {
        throw std::runtime_error("eventfd create error");
    }
    epoll_.addFd(wakeFd_, EPOLLIN | EPOLLET);
}

SubReactor::~SubReactor() {
    stop();
    if (wakeFd_ != -1) {
        close(wakeFd_);
        wakeFd_ = -1;
    }
}

void SubReactor::start() {
    running_.store(true);
    thread_ = std::thread([this]() { run(); });
}

void SubReactor::stop() {
    const bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        wakeup();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SubReactor::enqueueConnection(int clientFd) {
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingConnections_.push(clientFd);
    }
    wakeup();
}

void SubReactor::run() {
    if (logger_) {
        logger_->info("sub reactor #" + std::to_string(index_) + " started");
    }

    while (running_.load()) {
        processExpiredConnections();

        std::vector<epoll_event> activeEvents;
        epoll_.poll(activeEvents, timerManager_.nextPollTimeoutMs());
        for (const auto& event : activeEvents) {
            const int fd = event.data.fd;
            if (fd == wakeFd_) {
                handleWakeup();
                processPendingConnections();
                continue;
            }

            auto it = connections_.find(fd);
            if (it == connections_.end()) {
                continue;
            }

            auto& conn = it->second;
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                epoll_.delFd(fd);
                conn->closeConnection();
                connections_.erase(it);
                continue;
            }

            if (event.events & (EPOLLIN | EPOLLRDHUP)) {
                conn->handleRead(epoll_);
            } else if (event.events & EPOLLOUT) {
                conn->handleWrite(epoll_);
            }

            timerManager_.schedule(conn);
            if (conn->isClosed()) {
                epoll_.delFd(fd);
                connections_.erase(fd);
            }
        }

        processPendingConnections();
    }

    for (auto& entry : connections_) {
        entry.second->closeConnection();
    }
    connections_.clear();

    if (logger_) {
        logger_->info("sub reactor #" + std::to_string(index_) + " stopped");
    }
}

void SubReactor::processPendingConnections() {
    std::queue<int> localQueue;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        std::swap(localQueue, pendingConnections_);
    }

    while (!localQueue.empty()) {
        const int clientFd = localQueue.front();
        localQueue.pop();

        auto conn = std::make_shared<HttpConnection>(clientFd, config_, metrics_, logger_);
        connections_[clientFd] = conn;
        epoll_.addFd(clientFd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
        timerManager_.schedule(conn);
    }
}

void SubReactor::processExpiredConnections() {
    for (const auto& conn : timerManager_.collectExpired()) {
        const int fd = conn->getFd();
        if (fd == -1) {
            continue;
        }
        epoll_.delFd(fd);
        conn->closeForTimeout();
        connections_.erase(fd);
    }
}

void SubReactor::handleWakeup() {
    uint64_t counter = 0;
    while (true) {
        const ssize_t n = read(wakeFd_, &counter, sizeof(counter));
        if (n == sizeof(counter)) {
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n == -1 && errno == EINTR) {
            continue;
        }
        break;
    }
}

void SubReactor::wakeup() {
    uint64_t signal = kWakeSignal;
    const ssize_t n = write(wakeFd_, &signal, sizeof(signal));
    if (n == -1 && errno != EAGAIN) {
        if (logger_) {
            logger_->warn("sub reactor #" + std::to_string(index_) +
                          " wakeup failed: " + std::strerror(errno));
        }
    }
}
