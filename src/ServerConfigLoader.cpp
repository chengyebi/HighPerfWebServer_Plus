#include "ServerConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace {
std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}
}

bool ServerConfigLoader::loadFromFile(const std::string& path, ServerConfig& config, std::string& errorMessage) {
    std::ifstream input(path);
    if (!input.is_open()) {
        errorMessage = "failed to open config file: " + path;
        return false;
    }

    std::string line;
    size_t lineNo = 0;
    while (std::getline(input, line)) {
        ++lineNo;
        const auto commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            errorMessage = "invalid config line " + std::to_string(lineNo) + ": " + line;
            return false;
        }

        const std::string key = trim(line.substr(0, eqPos));
        const std::string value = trim(line.substr(eqPos + 1));
        try {
            if (key == "host") {
                config.host = value;
            } else if (key == "port") {
                config.port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "threads") {
                config.threadCount = static_cast<size_t>(std::stoul(value));
            } else if (key == "resources") {
                config.resourceRoot = value;
            } else if (key == "access_log") {
                config.accessLogPath = value;
            } else if (key == "error_log") {
                config.errorLogPath = value;
            } else if (key == "idle_timeout_ms") {
                config.idleTimeoutMs = std::stoi(value);
            }
        } catch (const std::exception&) {
            errorMessage = "invalid value for key '" + key + "' at line " + std::to_string(lineNo);
            return false;
        }
    }

    return true;
}
