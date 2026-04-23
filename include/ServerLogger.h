#pragma once

#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

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
    enum class LogTarget {
        Access,
        Error
    };

    struct LogItem {
        LogTarget target;
        bool echoToConsole;
        std::string line;
    };

    void enqueue(LogTarget target, bool echoToConsole, const std::string& line);
    void run();
    static std::string nowString();

    std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<LogItem> queue_;
    std::ofstream accessStream_;
    std::ofstream errorStream_;
    bool stopping_;
    std::thread worker_;
};
