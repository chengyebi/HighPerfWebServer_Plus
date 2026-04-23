#include "HttpRequest.h"

#include "Buffer.h"

#include <algorithm>
#include <cctype>

void HttpRequest::init() {
    method_.clear();
    path_.clear();
    version_.clear();
    body_.clear();
    state_ = REQUEST_LINE;
    headers_.clear();
}

bool HttpRequest::isKeepAlive() const {
    if (headers_.count("Connection")) {
        return headers_.find("Connection")->second == "keep-alive" && version_ == "HTTP/1.1";
    }
    return version_ == "HTTP/1.1";
}

bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    if (buff.readableBytes() <= 0) {
        return false;
    }

    while (buff.readableBytes() && state_ != FINISH) {
        const char* lineEnd = std::search(buff.peek(), buff.peek() + buff.readableBytes(), CRLF, CRLF + 2);
        if (lineEnd == buff.peek() + buff.readableBytes()) {
            break;
        }

        std::string line(buff.peek(), lineEnd);
        switch (state_) {
        case REQUEST_LINE:
            if (!parseRequestLine_(line)) {
                return false;
            }
            parsePath_();
            break;
        case HEADERS:
            parseHeader_(line);
            if (line.empty()) {
                state_ = FINISH;
            }
            break;
        case BODY:
            parseBody_(line);
            break;
        case FINISH:
            break;
        }
        buff.retrieveUntil(lineEnd + 2);
    }
    return true;
}

bool HttpRequest::parseRequestLine_(const std::string& line) {
    const size_t methodEnd = line.find(' ');
    if (methodEnd == std::string::npos) {
        return false;
    }
    method_ = line.substr(0, methodEnd);

    const size_t pathEnd = line.find(' ', methodEnd + 1);
    if (pathEnd == std::string::npos) {
        return false;
    }
    path_ = line.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    version_ = line.substr(pathEnd + 1);
    state_ = HEADERS;
    return true;
}

void HttpRequest::parseHeader_(const std::string& line) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return;
    }
    std::string key = line.substr(0, colon);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(c); });

    size_t valStart = colon + 1;
    while (valStart < line.size() && line[valStart] == ' ') {
        ++valStart;
    }
    std::string value = line.substr(valStart);
    if (key == "Connection") {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }
    headers_[key] = value;
}

void HttpRequest::parseBody_(const std::string& line) {
    body_ = line;
    state_ = FINISH;
}

void HttpRequest::parsePath_() {
    if (path_ == "/") {
        path_ = "/index.html";
    }
}

std::string HttpRequest::path() const {
    return path_;
}

std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    if (headers_.count(key)) {
        return headers_.at(key);
    }
    return "";
}
