#pragma once

#include <fstream>
#include <mutex>
#include <string>

class ServerLogger {
public:
    explicit ServerLogger(const std::string& logFilePath);
    ~ServerLogger();

    ServerLogger(const ServerLogger&) = delete;
    ServerLogger& operator=(const ServerLogger&) = delete;

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    void log(const std::string& level, const std::string& message);
    static std::string nowString();

    std::mutex mutex_;
    std::ofstream fileStream_;
};
