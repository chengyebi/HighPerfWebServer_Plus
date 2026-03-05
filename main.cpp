#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "ThreadPool.h"
#include "Socket.h"
#include "Epoll.h"
#include "InetAddress.h"
#include "HttpConnection.h"

int main() {
    //建立监听，
    Socket serv_sock;
    InetAddress serv_addr("127.0.0.1",8888);
    serv_sock.bind(serv_addr);
    serv_sock.listen();
    //初始化epoll
    Epoll ep;
    serv_sock.setNonBlocking();
    //监听Socket用ET模式，达到高性能处理
    ep.addFd(serv_sock.getFd(),EPOLLIN|EPOLLET);
    auto dead_fds=std::make_shared<std::vector<std::pair<int,std::weak_ptr<HttpConnection>>>>();
    auto dead_mtx=std::make_shared<std::mutex>();
    ThreadPool pool(8);
    //堆分配死亡名单和线程池
    //连接池，映射
    std::unordered_map<int,std::shared_ptr<HttpConnection>> connections;
    std::cout<<"HighPerfWebServer running on port 8888……"<<std::endl;
    while (true) {
        //主线程安全清理僵尸连接
        {
            std::lock_guard<std::mutex> lock(*dead_mtx);
            for (auto& dead_node:*dead_fds) {
                int dead_fd=dead_node.first;
                auto weak_conn=dead_node.second;
                //ABA免疫检查
                if (connections.count(dead_fd)&&connections[dead_fd]==weak_conn.lock()) {
                    connections.erase(dead_fd);
                }
            }
            dead_fds->clear();
        }
        std::vector<epoll_event> active_events;
        ep.poll(active_events,10);//10ms超时时间，防止阻塞
        for (auto& event: active_events) {
            int fd=event.data.fd;
            //场景A:有新的客户端连接
            if (fd==serv_sock.getFd()) {
                //ET模式必须循环accept
                while (true) {
                    InetAddress client_addr;
                    int client_fd=serv_sock.accept(client_addr);
                    if (client_fd==-1) {
                        if (errno==EAGAIN||errno==EWOULDBLOCK) {//全连接队列里等待的客户端已经被拿光,正常结束
                            break;
                        }
                        else break;//发生错误，异常，强制结束，这个地方没有直接在fd==-1时直接写break而是写了两个分支，是为了为后续的扩展功能占下空位
                    }
                    //创建连接对象并存入哈希表，智能指针管理生命周期
                    connections[client_fd]=std::make_shared<HttpConnection>(client_fd);
                    //注册客户端socket的可读事件
                    ep.addFd(client_fd,EPOLLIN|EPOLLET| EPOLLONESHOT| EPOLLRDHUP);
                }
            }
            //场景B：已有客户端发来数据或可写
            else {
                if (connections.count(fd)==0)
                    continue;
                auto conn=connections[fd];
                //1.拦截错误事件，
                if (event.events&(EPOLLERR|EPOLLHUP)) {
                    ep.delFd(fd);
                    std::lock_guard<std::mutex> lock(*dead_mtx);
                    dead_fds->push_back({fd,std::weak_ptr<HttpConnection>(conn)});
                    continue;
                }
                int events=event.events;
                //2.扔进线程池异步处理
                pool.addTask([conn,fd,events,&ep,dead_mtx,dead_fds]() {
                    if (events&(EPOLLIN|EPOLLRDHUP)) {
                        conn->handleRead(ep);
                    }
                    //排他性写，防止大文件双重触发写操作
                    else if (events& EPOLLOUT) {
                        conn->handleWrite(ep);
                    }
                    //3.worker处理完后检查并汇报死亡
                    if (conn->isClosed()) {
                        std::lock_guard<std::mutex> lock(*dead_mtx);
                        dead_fds->push_back({fd,std::weak_ptr<HttpConnection>(conn)});
                    }
                });
            }
        }


    }
    return 0;
}