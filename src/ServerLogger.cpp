#include "ServerLogger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

ServerLogger::ServerLogger(const std::string& accessLogPath, const std::string& errorLogPath)
    : accessEnabled_(false),
      errorEnabled_(false),
      stopping_(false) {
    if (!accessLogPath.empty()) {
        accessStream_.open(accessLogPath, std::ios::app);
        accessEnabled_ = accessStream_.is_open();
    }
    if (!errorLogPath.empty()) {
        errorStream_.open(errorLogPath, std::ios::app);
        errorEnabled_ = errorStream_.is_open();
    }
    worker_ = std::thread([this]() { run(); });
}

ServerLogger::~ServerLogger() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cond_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ServerLogger::access(const std::string& message) {
    if (!accessEnabled_) {
        return;
    }
    enqueue(LogTarget::Access, false, "[" + nowString() + "] [ACCESS] " + message);
}

void ServerLogger::info(const std::string& message) {
    enqueue(LogTarget::Error, true, "[" + nowString() + "] [INFO] " + message);
}

void ServerLogger::warn(const std::string& message) {
    enqueue(LogTarget::Error, true, "[" + nowString() + "] [WARN] " + message);
}

void ServerLogger::error(const std::string& message) {
    enqueue(LogTarget::Error, true, "[" + nowString() + "] [ERROR] " + message);
}

void ServerLogger::enqueue(LogTarget target, bool echoToConsole, const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(LogItem{target, echoToConsole, line});
    }
    cond_.notify_one();
}

void ServerLogger::run() {
    while (true) {
        std::deque<LogItem> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                break;
            }
            batch.swap(queue_);
        }

        for (const auto& item : batch) {
            if (item.echoToConsole) {
                std::cout << item.line << std::endl;
            }

            if (item.target == LogTarget::Access) {
                if (accessEnabled_) {
                    accessStream_ << item.line << '\n';
                }
            } else {
                if (errorEnabled_) {
                    errorStream_ << item.line << '\n';
                }
            }
        }

        if (accessEnabled_) {
            accessStream_.flush();
        }
        if (errorEnabled_) {
            errorStream_.flush();
        }
    }
}

std::string ServerLogger::nowString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &nowTime);
#else
    localtime_r(&nowTime, &localTm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
