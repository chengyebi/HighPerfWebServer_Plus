#include "Socket.h"

#include "InetAddress.h"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

Socket::Socket() : fd_(-1) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == -1) {
        throw std::runtime_error("socket create error");
    }
}

Socket::Socket(int fd) : fd_(fd) {
    if (fd_ == -1) {
        throw std::runtime_error("socket error: invalid fd");
    }
}

Socket::~Socket() {
    if (fd_ != -1) {
        close();
    }
}

void Socket::bind(const InetAddress& addr) {
    int opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (::bind(fd_, reinterpret_cast<const struct sockaddr*>(addr.getAddr()), addr.getAddrLen()) == -1) {
        throw std::runtime_error("socket bind error");
    }
}

void Socket::listen() {
    if (::listen(fd_, SOMAXCONN) == -1) {
        throw std::runtime_error("socket listen error");
    }
}

int Socket::accept(InetAddress& clientAddr) {
    struct sockaddr_in addr {};
    socklen_t len = sizeof(addr);
    const int clientFd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (clientFd != -1) {
        clientAddr.setAddr(addr, len);
    }
    return clientFd;
}

int Socket::acceptNonBlocking(InetAddress& clientAddr) {
    struct sockaddr_in addr {};
    socklen_t len = sizeof(addr);
#ifdef SOCK_NONBLOCK
    const int clientFd = ::accept4(fd_,
                                   reinterpret_cast<struct sockaddr*>(&addr),
                                   &len,
                                   SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    const int clientFd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (clientFd != -1) {
        int flags = fcntl(clientFd, F_GETFL);
        fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
    if (clientFd != -1) {
        clientAddr.setAddr(addr, len);
    }
    return clientFd;
}

void Socket::setNonBlocking() {
    const int flags = fcntl(fd_, F_GETFL);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

int Socket::getFd() const {
    return fd_;
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (fd_ != -1) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::close() {
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}
