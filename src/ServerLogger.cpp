#include "ServerLogger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

ServerLogger::ServerLogger(const std::string& logFilePath) {
    if (!logFilePath.empty()) {
        fileStream_.open(logFilePath, std::ios::app);
    }
}

ServerLogger::~ServerLogger() = default;

void ServerLogger::info(const std::string& message) {
    log("INFO", message);
}

void ServerLogger::warn(const std::string& message) {
    log("WARN", message);
}

void ServerLogger::error(const std::string& message) {
    log("ERROR", message);
}

void ServerLogger::log(const std::string& level, const std::string& message) {
    const std::string line = "[" + nowString() + "] [" + level + "] " + message;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << line << std::endl;
    if (fileStream_.is_open()) {
        fileStream_ << line << '\n';
        fileStream_.flush();
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
