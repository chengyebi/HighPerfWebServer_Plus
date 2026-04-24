#include "StaticFileCache.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

StaticFileCache::StaticFileCache(std::string resourceRoot)
    : resourceRoot_(std::move(resourceRoot)) {}

std::shared_ptr<const StaticFileCache::Entry> StaticFileCache::get(const std::string& requestPath,
                                                                   const std::string& contentType) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(requestPath);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    auto loaded = loadFile(requestPath, contentType);
    if (!loaded) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = cache_.emplace(requestPath, loaded);
    return inserted ? loaded : it->second;
}

void StaticFileCache::warmup(const std::string& requestPath, const std::string& contentType) {
    (void)get(requestPath, contentType);
}

std::shared_ptr<const StaticFileCache::Entry> StaticFileCache::loadFile(const std::string& requestPath,
                                                                        const std::string& contentType) const {
    const std::string filePath = resourceRoot_ + requestPath;
    struct stat fileStat {};
    if (stat(filePath.c_str(), &fileStat) < 0 || !S_ISREG(fileStat.st_mode)) {
        return nullptr;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return nullptr;
    }

    std::ostringstream oss;
    oss << input.rdbuf();
    auto entry = std::make_shared<Entry>();
    entry->content = oss.str();
    entry->contentType = contentType;
    return entry;
}
