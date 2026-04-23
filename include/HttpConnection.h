#pragma once

#include "Buffer.h"
#include "Epoll.h"
#include "HttpRequest.h"
#include "ServerConfig.h"
#include "ServerLogger.h"
#include "ServerMetrics.h"
#include "Socket.h"
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>

class HttpConnection {
public:
    HttpConnection(int fd,
                   const ServerConfig& config,
                   std::shared_ptr<ServerMetrics> metrics,
                   std::shared_ptr<ServerLogger> logger);
    ~HttpConnection();

    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;

    int getFd() const;
    void handleRead(Epoll& ep);
    void handleWrite(Epoll& ep);
    bool isClosed() const;
    bool isIdle(int64_t nowMs, int idleTimeoutMs) const;
    void closeForTimeout();
    int64_t lastActiveMs() const;
    void closeConnection();

private:
    void process();
    void touchActivity();
    void appendResponse(const std::string& status,
                        const std::string& contentType,
                        const std::string& body,
                        bool includeBody);
    void serveMetrics(bool includeBody);
    std::string resolveRequestPath() const;
    static std::string detectMimeType(const std::string& path);

    Socket socket_;
    Buffer inputBuffer_;
    Buffer outputBuffer_;
    HttpRequest request_;
    bool keepAlive_;
    bool headRequest_;
    bool responseReady_;
    bool activeTracked_;
    int fileFd_;
    off_t fileOffset_;
    size_t fileLen_;
    ServerConfig config_;
    std::shared_ptr<ServerMetrics> metrics_;
    std::shared_ptr<ServerLogger> logger_;
    std::atomic<int64_t> lastActiveMs_;
};
