#include "ServerLogger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

ServerLogger::ServerLogger(const std::string& accessLogPath, const std::string& errorLogPath) {
    if (!accessLogPath.empty()) {
        accessStream_.open(accessLogPath, std::ios::app);
    }
    if (!errorLogPath.empty()) {
        errorStream_.open(errorLogPath, std::ios::app);
    }
}

ServerLogger::~ServerLogger() = default;

void ServerLogger::access(const std::string& message) {
    logAccess(message);
}

void ServerLogger::info(const std::string& message) {
    logErrorLike("INFO", message);
}

void ServerLogger::warn(const std::string& message) {
    logErrorLike("WARN", message);
}

void ServerLogger::error(const std::string& message) {
    logErrorLike("ERROR", message);
}

void ServerLogger::logAccess(const std::string& message) {
    const std::string line = "[" + nowString() + "] [ACCESS] " + message;
    std::lock_guard<std::mutex> lock(mutex_);
    if (accessStream_.is_open()) {
        accessStream_ << line << '\n';
        accessStream_.flush();
    }
}

void ServerLogger::logErrorLike(const std::string& level, const std::string& message) {
    const std::string line = "[" + nowString() + "] [" + level + "] " + message;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << line << std::endl;
    if (errorStream_.is_open()) {
        errorStream_ << line << '\n';
        errorStream_.flush();
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
