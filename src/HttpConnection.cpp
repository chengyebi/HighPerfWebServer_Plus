#include "HttpConnection.h"

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <sstream>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string buildHeader(const std::string& status,
                        const std::string& contentType,
                        size_t contentLength,
                        bool keepAlive) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << contentLength << "\r\n"
        << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n"
        << "Server: HighPerfWebServer/2.0\r\n\r\n";
    return oss.str();
}
}

HttpConnection::HttpConnection(int fd,
                               const ServerConfig& config,
                               std::shared_ptr<ServerMetrics> metrics,
                               std::shared_ptr<ServerLogger> logger)
    : socket_(fd),
      keepAlive_(false),
      headRequest_(false),
      responseReady_(false),
      activeTracked_(true),
      fileFd_(-1),
      fileOffset_(0),
      fileLen_(0),
      config_(config),
      metrics_(std::move(metrics)),
      logger_(std::move(logger)),
      lastActiveMs_(nowMs()) {
    socket_.setNonBlocking();
}

HttpConnection::~HttpConnection() {
    if (fileFd_ != -1) {
        close(fileFd_);
        fileFd_ = -1;
    }
    if (activeTracked_ && metrics_) {
        metrics_->onClose();
        activeTracked_ = false;
    }
}

int HttpConnection::getFd() const {
    return socket_.getFd();
}

bool HttpConnection::isClosed() const {
    return socket_.getFd() == -1;
}

int64_t HttpConnection::lastActiveMs() const {
    return lastActiveMs_.load(std::memory_order_relaxed);
}

bool HttpConnection::isIdle(int64_t nowMsValue, int idleTimeoutMs) const {
    return socket_.getFd() != -1 &&
           idleTimeoutMs > 0 &&
           (nowMsValue - lastActiveMs_.load(std::memory_order_relaxed)) >= idleTimeoutMs;
}

void HttpConnection::closeForTimeout() {
    if (metrics_) {
        metrics_->onTimeoutClose();
    }
    if (logger_) {
        logger_->warn("closed idle connection fd=" + std::to_string(socket_.getFd()));
    }
    closeConnection();
}

void HttpConnection::closeConnection() {
    if (socket_.getFd() != -1) {
        socket_.close();
        if (activeTracked_ && metrics_) {
            metrics_->onClose();
            activeTracked_ = false;
        }
    }
}

void HttpConnection::touchActivity() {
    lastActiveMs_.store(nowMs(), std::memory_order_relaxed);
}

void HttpConnection::logAccess(const std::string& method,
                               const std::string& path,
                               int statusCode,
                               size_t responseBytes) {
    if (logger_) {
        logger_->access(method + " " + path +
                        " -> " + std::to_string(statusCode) +
                        " bytes=" + std::to_string(responseBytes));
    }
}

void HttpConnection::appendResponse(const std::string& status,
                                    const std::string& contentType,
                                    const std::string& body,
                                    bool includeBody) {
    touchActivity();
    outputBuffer_.append(buildHeader(status, contentType, body.size(), keepAlive_));
    if (includeBody && !body.empty()) {
        outputBuffer_.append(body);
    }
    responseReady_ = true;
    if (metrics_) {
        metrics_->onResponse();
    }
}

void HttpConnection::serveMetrics(bool includeBody) {
    touchActivity();
    if (metrics_) {
        metrics_->onResponse();
    }
    const std::string body = metrics_ ? metrics_->toJson() : "{}";
    outputBuffer_.append(buildHeader("200 OK", "application/json", body.size(), keepAlive_));
    if (includeBody && !body.empty()) {
        outputBuffer_.append(body);
    }
    responseReady_ = true;
}

std::string HttpConnection::resolveRequestPath() const {
    std::string path = request_.path();
    const auto queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        path = path.substr(0, queryPos);
    }
    const auto fragmentPos = path.find('#');
    if (fragmentPos != std::string::npos) {
        path = path.substr(0, fragmentPos);
    }
    if (path.empty()) {
        path = "/index.html";
    }
    return path;
}

std::string HttpConnection::detectMimeType(const std::string& path) {
    const auto dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "text/plain; charset=utf-8";
    }
    const std::string ext = path.substr(dotPos);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

void HttpConnection::handleRead(Epoll& ep) {
    int saveErrno = 0;
    ssize_t len = 0;
    while (true) {
        len = inputBuffer_.readFd(socket_.getFd(), &saveErrno);
        if (len > 0 && metrics_) {
            metrics_->addBytesRead(static_cast<size_t>(len));
            touchActivity();
        }
        if (len <= 0) {
            if (saveErrno == EAGAIN || saveErrno == EWOULDBLOCK) {
                break;
            }
            if (saveErrno == EINTR) {
                continue;
            }
            closeConnection();
            return;
        }
    }

    process();

    if (outputBuffer_.readableBytes() > 0 || fileFd_ != -1) {
        handleWrite(ep);
    } else if (!isClosed()) {
        ep.modFd(socket_.getFd(), EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
    }
}

void HttpConnection::handleWrite(Epoll& ep) {
    ssize_t len = 0;
    int saveErrno = 0;

    while (true) {
        if (outputBuffer_.readableBytes() > 0) {
            while (outputBuffer_.readableBytes() > 0) {
                len = outputBuffer_.writeFd(socket_.getFd(), &saveErrno);
                if (len > 0 && metrics_) {
                    metrics_->addBytesWritten(static_cast<size_t>(len));
                    touchActivity();
                }
                if (len <= 0) {
                    if (saveErrno == EAGAIN || saveErrno == EWOULDBLOCK) {
                        ep.modFd(socket_.getFd(), EPOLLIN | EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
                        return;
                    }
                    if (saveErrno == EINTR) {
                        continue;
                    }
                    closeConnection();
                    return;
                }
            }
        }

        if (outputBuffer_.readableBytes() == 0 && fileFd_ != -1) {
            while (true) {
                const ssize_t sent = sendfile(socket_.getFd(), fileFd_, &fileOffset_, fileLen_ - fileOffset_);
                if (sent > 0) {
                    if (metrics_) {
                        metrics_->addBytesWritten(static_cast<size_t>(sent));
                    }
                    touchActivity();
                    if (fileOffset_ >= static_cast<off_t>(fileLen_)) {
                        close(fileFd_);
                        fileFd_ = -1;
                        fileLen_ = 0;
                        fileOffset_ = 0;
                        break;
                    }
                    continue;
                }
                if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    ep.modFd(socket_.getFd(), EPOLLIN | EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
                    return;
                }
                if (sent == -1 && errno == EINTR) {
                    continue;
                }
                if (fileFd_ != -1) {
                    close(fileFd_);
                    fileFd_ = -1;
                }
                fileLen_ = 0;
                fileOffset_ = 0;
                closeConnection();
                return;
            }
        }

        if (outputBuffer_.readableBytes() == 0 && fileFd_ == -1) {
            if (keepAlive_) {
                request_.init();
                responseReady_ = false;
                if (inputBuffer_.readableBytes() > 0) {
                    process();
                    if (outputBuffer_.readableBytes() > 0 || fileFd_ != -1) {
                        continue;
                    }
                }
                ep.modFd(socket_.getFd(), EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
                return;
            }
            closeConnection();
            return;
        }
    }
}

void HttpConnection::process() {
    if (!request_.parse(inputBuffer_)) {
        keepAlive_ = false;
        if (metrics_) {
            metrics_->onClientError();
        }
        appendResponse("400 Bad Request", "text/html; charset=utf-8", "<h1>400 Bad Request</h1>", !headRequest_);
        logAccess("INVALID", "-", 400, outputBuffer_.readableBytes());
        return;
    }
    if (!request_.isComplete() || responseReady_) {
        return;
    }

    keepAlive_ = request_.isKeepAlive();
    headRequest_ = request_.method() == "HEAD";

    if (request_.method() != "GET" && request_.method() != "HEAD") {
        keepAlive_ = false;
        if (metrics_) {
            metrics_->onClientError();
        }
        appendResponse("405 Method Not Allowed",
                       "text/plain; charset=utf-8",
                       "Only GET and HEAD are supported.\n",
                       !headRequest_);
        logAccess(request_.method(), request_.path(), 405, outputBuffer_.readableBytes());
        if (logger_) {
            logger_->warn(request_.method() + " " + request_.path() + " -> 405");
        }
        return;
    }

    if (metrics_) {
        metrics_->onRequest();
    }

    const std::string path = resolveRequestPath();
    if (path.find("..") != std::string::npos) {
        if (metrics_) {
            metrics_->onClientError();
        }
        appendResponse("403 Forbidden", "text/html; charset=utf-8", "<h1>403 Forbidden</h1>", !headRequest_);
        logAccess(request_.method(), path, 403, outputBuffer_.readableBytes());
        if (logger_) {
            logger_->warn(request_.method() + " " + path + " -> 403");
        }
        return;
    }

    if (path == "/healthz") {
        appendResponse("200 OK", "text/plain; charset=utf-8", "ok\n", !headRequest_);
        logAccess(request_.method(), path, 200, outputBuffer_.readableBytes());
        return;
    }

    if (path == "/metrics") {
        serveMetrics(!headRequest_);
        logAccess(request_.method(), path, 200, outputBuffer_.readableBytes());
        return;
    }

    const std::string filePath = config_.resourceRoot + path;
    struct stat fileStat {};
    if (stat(filePath.c_str(), &fileStat) < 0 || S_ISDIR(fileStat.st_mode)) {
        if (metrics_) {
            metrics_->onClientError();
        }
        appendResponse("404 Not Found", "text/html; charset=utf-8", "<h1>404 Not Found</h1>", !headRequest_);
        logAccess(request_.method(), path, 404, outputBuffer_.readableBytes());
        if (logger_) {
            logger_->warn(request_.method() + " " + path + " -> 404");
        }
        return;
    }

    if (headRequest_) {
        outputBuffer_.append(buildHeader("200 OK", detectMimeType(path), static_cast<size_t>(fileStat.st_size), keepAlive_));
        touchActivity();
        responseReady_ = true;
        if (metrics_) {
            metrics_->onResponse();
        }
        logAccess(request_.method(), path, 200, outputBuffer_.readableBytes());
        return;
    }

    fileFd_ = open(filePath.c_str(), O_RDONLY);
    if (fileFd_ < 0) {
        if (metrics_) {
            metrics_->onServerError();
        }
        appendResponse("500 Internal Server Error",
                       "text/html; charset=utf-8",
                       "<h1>500 Internal Server Error</h1>",
                       true);
        logAccess(request_.method(), path, 500, outputBuffer_.readableBytes());
        if (logger_) {
            logger_->error(request_.method() + " " + path + " -> 500");
        }
        return;
    }

    outputBuffer_.append(buildHeader("200 OK", detectMimeType(path), static_cast<size_t>(fileStat.st_size), keepAlive_));
    touchActivity();
    fileLen_ = static_cast<size_t>(fileStat.st_size);
    fileOffset_ = 0;
    responseReady_ = true;
    if (metrics_) {
        metrics_->onResponse();
    }
    logAccess(request_.method(), path, 200, fileLen_ + outputBuffer_.readableBytes());
}
