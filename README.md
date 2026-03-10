# HighPerfWebServer

基于 C++11 与 Epoll 的高并发 HTTP 服务器。在 8 核环境、2 万并发连接压测下，峰值吞吐量达 **186,000 QPS**。

## 项目架构

```
HighPerfWebServer/
├── include/
│   ├── Buffer.h          # 动态缓冲区
│   ├── Epoll.h           # Epoll 封装
│   ├── HttpConnection.h  # HTTP 连接管理
│   ├── HttpRequest.h     # HTTP 请求解析
│   ├── InetAddress.h     # 网络地址封装
│   ├── Socket.h          # Socket RAII 封装
│   └── ThreadPool.h      # 固定大小线程池
├── src/
│   ├── Buffer.cpp
│   ├── Epoll.cpp
│   ├── HttpConnection.cpp
│   ├── HttpRequest.cpp
│   ├── InetAddress.cpp
│   └── Socket.cpp
├── resources/
│   └── index.html        # 静态资源目录
├── main.cpp
└── CMakeLists.txt
```

## 技术亮点

**并发模型**

采用 Epoll ET 模式结合非阻塞 I/O 实现 Reactor 架构。引入 `EPOLLONESHOT` 标志，确保同一连接在任意时刻仅由单一工作线程处理，彻底消除多线程对同一 fd 的竞态条件。

**连接管理与 ABA 免疫**

封装 `Socket` 类实现 RAII 资源自动管理。主线程通过 `std::weak_ptr` 配合延迟清理队列对连接存活状态进行二次校验——工作线程将死亡连接的 `{fd, weak_ptr}` 对放入死亡名单，主线程在下一轮事件循环开始前集中清理，规避高并发下 fd 被复用导致的 ABA 问题。

**线程池**

实现固定大小线程池，以 `std::mutex` 保护任务队列，`std::condition_variable` 精细管理线程唤醒。析构时通过手动控制 `lock_guard` 作用域，确保设置关闭标志与唤醒线程之间不存在不必要的锁竞争。

**协议解析**

基于有限状态机（FSM）实现 HTTP/1.1 协议解析器，状态流转为 `REQUEST_LINE → HEADERS → BODY → FINISH`。严密处理 TCP 粘包/拆包边界（`parse()` 返回 `true` 不代表请求完整，需再判断 `isComplete()`），以及 `EPOLLRDHUP` 半关闭连接的异常清理。

**性能优化**

- **ET 读完整性**：通过 `while` 循环非阻塞 `recv`，直至返回 `EAGAIN`，彻底抽干内核缓冲区，避免 ET 模式下的事件丢失
- **零拷贝传输**：调用 `sendfile()` 系统调用实现内核级文件传输，绕过用户态拷贝，降低上下文切换开销
- **分散读**：使用 `readv()` 将数据同时读入 Buffer 内部空间与栈上临时数组，减少系统调用次数
- **Pipeline 支持**：长连接处理完一个请求后主动检查 Buffer 是否有残留数据，防止 Pipeline 模式下的连接假死

## 快速开始

**环境要求**：Linux、GCC 7+、CMake 3.10+

```bash
git clone https://github.com/chengyebi/HighPerfWebServer.git
cd HighPerfWebServer
mkdir build && cd build
cmake .. && make
./server
```

服务器默认监听 `127.0.0.1:8888`，静态资源放在项目根目录的 `resources/` 下。

```bash
# 访问测试
curl http://127.0.0.1:8888/

# 压测（需安装 wrk）
wrk -t8 -c20000 -d30s http://127.0.0.1:8888/
```

## 核心模块说明

| 模块 | 说明 |
|------|------|
| `Buffer` | 基于 `vector<char>` 的动态缓冲区，支持 `readv` 分散读与自动扩容 |
| `Epoll` | 封装 `epoll_create1/ctl/wait`，禁止拷贝，独占 epfd 资源 |
| `Socket` | RAII 封装 socket fd，支持移动语义，禁止拷贝，防止 double close |
| `ThreadPool` | 固定大小线程池，`shared_ptr<Pool>` 保证线程存活期间 Pool 不被析构 |
| `HttpRequest` | FSM 状态机解析 HTTP/1.1，支持 keep-alive 与基础路径安全校验 |
| `HttpConnection` | 整合上述模块，管理单条连接的完整读写生命周期 |
