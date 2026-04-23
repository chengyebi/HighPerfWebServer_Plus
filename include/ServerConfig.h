#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct ServerConfig {
    std::string host = "0.0.0.0";
    uint16_t port = 8888;
    size_t threadCount = 8;
    std::string resourceRoot = "./resources";
    std::string logFilePath = "./server.log";
    int idleTimeoutMs = 15000;
};
