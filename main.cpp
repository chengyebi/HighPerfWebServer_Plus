#include <atomic>
#include <cerrno>
#include <chrono>
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
#include "IdleTimerManager.h"
#include "InetAddress.h"
#include "ServerConfig.h"
#include "ServerLogger.h"
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
              << " [--host IP] [--port PORT] [--threads N] [--resources PATH]"
              << " [--log PATH] [--idle-timeout-ms N]\n";
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
        } else if (arg == "--log" && i + 1 < argc) {
            config.logFilePath = argv[++i];
        } else if (arg == "--idle-timeout-ms" && i + 1 < argc) {
            config.idleTimeoutMs = std::stoi(argv[++i]);
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
    auto logger = std::make_shared<ServerLogger>(config.logFilePath);

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
    IdleTimerManager timerManager(config.idleTimeoutMs);
    std::unordered_map<int, std::shared_ptr<HttpConnection>> connections;

    logger->info("HighPerfWebServer_Plus listening on " + config.host + ":" + std::to_string(config.port) +
                 " with " + std::to_string(config.threadCount) + " worker threads");
    logger->info("static root=" + config.resourceRoot +
                 " log=" + config.logFilePath +
                 " idle_timeout_ms=" + std::to_string(config.idleTimeoutMs));

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

        for (const auto& conn : timerManager.collectExpired()) {
            const int fd = conn->getFd();
            if (fd == -1) {
                continue;
            }
            ep.delFd(fd);
            conn->closeForTimeout();
            std::lock_guard<std::mutex> lock(*deadMtx);
            deadFds->push_back({fd, std::weak_ptr<HttpConnection>(conn)});
        }

        std::vector<epoll_event> activeEvents;
        ep.poll(activeEvents, timerManager.nextPollTimeoutMs());
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
                    logger->info("accepted connection fd=" + std::to_string(clientFd));
                    connections[clientFd] = std::make_shared<HttpConnection>(clientFd, config, metrics, logger);
                    timerManager.schedule(connections[clientFd]);
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
            pool.addTask([conn, fd, events, &ep, &timerManager, deadMtx, deadFds]() {
                if (events & (EPOLLIN | EPOLLRDHUP)) {
                    conn->handleRead(ep);
                } else if (events & EPOLLOUT) {
                    conn->handleWrite(ep);
                }
                timerManager.schedule(conn);
                if (conn->isClosed()) {
                    std::lock_guard<std::mutex> lock(*deadMtx);
                    deadFds->push_back({fd, std::weak_ptr<HttpConnection>(conn)});
                }
            });
        }
    }

    logger->info("server shutting down");
    return 0;
}
