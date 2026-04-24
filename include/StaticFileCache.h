#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class StaticFileCache {
public:
    struct Entry {
        std::string content;
        std::string contentType;
    };

    explicit StaticFileCache(std::string resourceRoot);

    std::shared_ptr<const Entry> get(const std::string& requestPath, const std::string& contentType);
    void warmup(const std::string& requestPath, const std::string& contentType);

private:
    std::shared_ptr<const Entry> loadFile(const std::string& requestPath, const std::string& contentType) const;

    std::string resourceRoot_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<const Entry>> cache_;
};
