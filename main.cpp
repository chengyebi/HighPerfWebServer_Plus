#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "Epoll.h"
#include "InetAddress.h"
#include "ServerConfig.h"
#include "ServerConfigLoader.h"
#include "ServerLogger.h"
#include "ServerMetrics.h"
#include "Socket.h"
#include "SubReactor.h"

namespace {
std::atomic<bool> gRunning{true};

void handleSignal(int) {
    gRunning.store(false);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program
              << " [--host IP] [--port PORT] [--threads N] [--resources PATH]"
              << " [--access-log PATH] [--error-log PATH] [--idle-timeout-ms N]"
              << " [--config PATH]\n";
}

bool loadConfigFromArgs(int argc, char* argv[], ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            std::string errorMessage;
            if (!ServerConfigLoader::loadFromFile(argv[++i], config, errorMessage)) {
                std::cerr << errorMessage << '\n';
                return false;
            }
        }
    }
    return true;
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
        } else if (arg == "--access-log" && i + 1 < argc) {
            config.accessLogPath = argv[++i];
        } else if (arg == "--error-log" && i + 1 < argc) {
            config.errorLogPath = argv[++i];
        } else if (arg == "--idle-timeout-ms" && i + 1 < argc) {
            config.idleTimeoutMs = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            ++i;
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
    if (!loadConfigFromArgs(argc, argv, config)) {
        return 1;
    }
    if (!parseArgs(argc, argv, config)) {
        return 0;
    }

    auto metrics = std::make_shared<ServerMetrics>();
    auto logger = std::make_shared<ServerLogger>(config.accessLogPath, config.errorLogPath);
    if (config.threadCount == 0) {
        config.threadCount = 1;
    }

    Socket serverSocket;
    InetAddress serverAddress(config.host.c_str(), config.port);
    serverSocket.bind(serverAddress);
    serverSocket.listen();
    serverSocket.setNonBlocking();

    Epoll acceptorEpoll;
    acceptorEpoll.addFd(serverSocket.getFd(), EPOLLIN | EPOLLET);

    std::vector<std::unique_ptr<SubReactor>> subReactors;
    subReactors.reserve(config.threadCount);
    for (size_t i = 0; i < config.threadCount; ++i) {
        subReactors.push_back(std::make_unique<SubReactor>(i, config, metrics, logger));
        subReactors.back()->start();
    }

    logger->info("HighPerfWebServer_Plus listening on " + config.host + ":" + std::to_string(config.port) +
                 " with main-sub reactor model and " + std::to_string(config.threadCount) + " sub reactors");
    logger->info("static root=" + config.resourceRoot +
                 " access_log=" + config.accessLogPath +
                 " error_log=" + config.errorLogPath +
                 " idle_timeout_ms=" + std::to_string(config.idleTimeoutMs));

    size_t nextReactorIndex = 0;
    while (gRunning.load()) {
        std::vector<epoll_event> activeEvents;
        acceptorEpoll.poll(activeEvents, 100);
        for (const auto& event : activeEvents) {
            if (event.data.fd != serverSocket.getFd()) {
                continue;
            }

            while (true) {
                InetAddress clientAddress;
                const int clientFd = serverSocket.acceptNonBlocking(clientAddress);
                if (clientFd == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    logger->warn("accept failed: " + std::string(std::strerror(errno)));
                    break;
                }

                metrics->onAccept();
                if (subReactors.empty()) {
                    logger->error("no sub reactor available, dropping fd=" + std::to_string(clientFd));
                    ::close(clientFd);
                    break;
                }

                auto& target = subReactors[nextReactorIndex % subReactors.size()];
                target->enqueueConnection(clientFd);
                ++nextReactorIndex;
            }
        }
    }

    logger->info("server shutting down");
    for (auto& reactor : subReactors) {
        reactor->stop();
    }
    return 0;
}
