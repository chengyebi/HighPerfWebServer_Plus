#pragma once

#include "Buffer.h"
#include "Epoll.h"
#include "HttpRequest.h"
#include "ServerConfig.h"
#include "ServerMetrics.h"
#include "Socket.h"
#include <memory>
#include <string>

class HttpConnection {
public:
    HttpConnection(int fd, const ServerConfig& config, std::shared_ptr<ServerMetrics> metrics);
    ~HttpConnection();

    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;

    int getFd() const;
    void handleRead(Epoll& ep);
    void handleWrite(Epoll& ep);
    bool isClosed() const;

private:
    void process();
    void closeConnection();
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
};
