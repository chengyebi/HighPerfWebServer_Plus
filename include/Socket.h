#pragma once

class InetAddress;

class Socket {
private:
    int fd_;

public:
    Socket();
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    void bind(const InetAddress& addr);
    void listen();
    [[nodiscard]] int accept(InetAddress& clientAddr);
    [[nodiscard]] int acceptNonBlocking(InetAddress& clientAddr);
    void setNonBlocking();
    int getFd() const;
    void close();
};
