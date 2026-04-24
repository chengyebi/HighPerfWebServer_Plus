#include "ServerConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
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
    namespace fs = std::filesystem;
    const fs::path configPath = fs::absolute(path);
    std::ifstream input(configPath);
    if (!input.is_open()) {
        errorMessage = "failed to open config file: " + path;
        return false;
    }
    config.configFilePath = configPath.string();
    const fs::path configDir = configPath.parent_path();

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
        std::string value = trim(line.substr(eqPos + 1));
        try {
            if (key == "host") {
                config.host = value;
            } else if (key == "port") {
                config.port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "threads") {
                config.threadCount = static_cast<size_t>(std::stoul(value));
            } else if (key == "resources") {
                config.resourceRoot = (configDir / fs::path(value)).lexically_normal().string();
            } else if (key == "access_log") {
                config.accessLogPath = (configDir / fs::path(value)).lexically_normal().string();
            } else if (key == "error_log") {
                config.errorLogPath = (configDir / fs::path(value)).lexically_normal().string();
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
