#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epoll.h"
#include "HttpConnection.h"
#include "InetAddress.h"
#include "ServerConfig.h"
#include "ServerMetrics.h"
#include "Socket.h"
#include "ThreadPool.h"

namespace {
std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running.store(false);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program
              << " [--host IP] [--port PORT] [--threads N] [--resources PATH]\n";
}

bool parseArgs(int argc, char* argv[], ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threadCount = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--resources" && i + 1 < argc) {
            config.resourceRoot = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    ServerConfig config;
    if (!parseArgs(argc, argv, config)) {
        return 0;
    }

    auto metrics = std::make_shared<ServerMetrics>();

    Socket servSock;
    InetAddress servAddr(config.host.c_str(), config.port);
    servSock.bind(servAddr);
    servSock.listen();
    servSock.setNonBlocking();

    Epoll ep;
    ep.addFd(servSock.getFd(), EPOLLIN | EPOLLET);

    auto deadFds = std::make_shared<std::vector<std::pair<int, std::weak_ptr<HttpConnection>>>>();
    auto deadMtx = std::make_shared<std::mutex>();
    ThreadPool pool(config.threadCount);
    std::unordered_map<int, std::shared_ptr<HttpConnection>> connections;

    std::cout << "HighPerfWebServer listening on " << config.host << ':' << config.port
              << " with " << config.threadCount << " worker threads\n";
    std::cout << "Static root: " << config.resourceRoot << '\n';
    std::cout << "Built-in endpoints: /healthz , /metrics\n";

    while (g_running.load()) {
        {
            std::lock_guard<std::mutex> lock(*deadMtx);
            for (const auto& deadNode : *deadFds) {
                const int deadFd = deadNode.first;
                const auto weakConn = deadNode.second;
                if (connections.count(deadFd) && connections[deadFd] == weakConn.lock()) {
                    connections.erase(deadFd);
                }
            }
            deadFds->clear();
        }

        std::vector<epoll_event> activeEvents;
        ep.poll(activeEvents, 100);
        for (const auto& event : activeEvents) {
            const int fd = event.data.fd;
            if (fd == servSock.getFd()) {
                while (true) {
                    InetAddress clientAddr;
                    const int clientFd = servSock.accept(clientAddr);
                    if (clientFd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        break;
                    }

                    metrics->onAccept();
                    connections[clientFd] = std::make_shared<HttpConnection>(clientFd, config, metrics);
                    ep.addFd(clientFd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
                }
                continue;
            }

            if (connections.count(fd) == 0) {
                continue;
            }

            auto conn = connections[fd];
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                ep.delFd(fd);
                std::lock_guard<std::mutex> lock(*deadMtx);
                deadFds->push_back({fd, std::weak_ptr<HttpConnection>(conn)});
                continue;
            }

            const uint32_t events = event.events;
            pool.addTask([conn, fd, events, &ep, deadMtx, deadFds]() {
                if (events & (EPOLLIN | EPOLLRDHUP)) {
                    conn->handleRead(ep);
                } else if (events & EPOLLOUT) {
                    conn->handleWrite(ep);
                }
                if (conn->isClosed()) {
                    std::lock_guard<std::mutex> lock(*deadMtx);
                    deadFds->push_back({fd, std::weak_ptr<HttpConnection>(conn)});
                }
            });
        }
    }

    std::cout << "Server shutting down\n";
    return 0;
}
