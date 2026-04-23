#pragma once

#include "ServerConfig.h"
#include <string>

class ServerConfigLoader {
public:
    static bool loadFromFile(const std::string& path, ServerConfig& config, std::string& errorMessage);
};
