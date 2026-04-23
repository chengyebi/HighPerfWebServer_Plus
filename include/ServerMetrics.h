#pragma once

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>

class ServerMetrics {
public:
    ServerMetrics() : startTime_(std::chrono::steady_clock::now()) {}

    void onAccept() {
        acceptedConnections_.fetch_add(1, std::memory_order_relaxed);
        activeConnections_.fetch_add(1, std::memory_order_relaxed);
    }

    void onClose() {
        activeConnections_.fetch_sub(1, std::memory_order_relaxed);
    }

    void onRequest() {
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
    }

    void onResponse() {
        totalResponses_.fetch_add(1, std::memory_order_relaxed);
    }

    void onClientError() {
        totalClientErrors_.fetch_add(1, std::memory_order_relaxed);
    }

    void onServerError() {
        totalServerErrors_.fetch_add(1, std::memory_order_relaxed);
    }

    void addBytesRead(size_t bytes) {
        bytesRead_.fetch_add(bytes, std::memory_order_relaxed);
    }

    void addBytesWritten(size_t bytes) {
        bytesWritten_.fetch_add(bytes, std::memory_order_relaxed);
    }

    std::string toJson() const {
        const auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
        std::ostringstream oss;
        oss << "{"
            << "\"uptime_ms\":" << uptimeMs << ","
            << "\"accepted_connections\":" << acceptedConnections_.load(std::memory_order_relaxed) << ","
            << "\"active_connections\":" << activeConnections_.load(std::memory_order_relaxed) << ","
            << "\"total_requests\":" << totalRequests_.load(std::memory_order_relaxed) << ","
            << "\"total_responses\":" << totalResponses_.load(std::memory_order_relaxed) << ","
            << "\"total_client_errors\":" << totalClientErrors_.load(std::memory_order_relaxed) << ","
            << "\"total_server_errors\":" << totalServerErrors_.load(std::memory_order_relaxed) << ","
            << "\"bytes_read\":" << bytesRead_.load(std::memory_order_relaxed) << ","
            << "\"bytes_written\":" << bytesWritten_.load(std::memory_order_relaxed)
            << "}";
        return oss.str();
    }

private:
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<uint64_t> acceptedConnections_{0};
    std::atomic<uint64_t> activeConnections_{0};
    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> totalResponses_{0};
    std::atomic<uint64_t> totalClientErrors_{0};
    std::atomic<uint64_t> totalServerErrors_{0};
    std::atomic<uint64_t> bytesRead_{0};
    std::atomic<uint64_t> bytesWritten_{0};
};
