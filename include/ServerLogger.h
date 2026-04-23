#pragma once

#include <fstream>
#include <mutex>
#include <string>

class ServerLogger {
public:
    ServerLogger(const std::string& accessLogPath, const std::string& errorLogPath);
    ~ServerLogger();

    ServerLogger(const ServerLogger&) = delete;
    ServerLogger& operator=(const ServerLogger&) = delete;

    void access(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    void logAccess(const std::string& message);
    void logErrorLike(const std::string& level, const std::string& message);
    static std::string nowString();

    std::mutex mutex_;
    std::ofstream accessStream_;
    std::ofstream errorStream_;
};
