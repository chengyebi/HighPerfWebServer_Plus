# HighPerfWebServer_Plus

`HighPerfWebServer_Plus` 是基于原始项目 [HighPerfWebServer](https://github.com/chengyebi/HighPerfWebServer) 持续迭代出的高并发服务器 Plus 版。

这个版本不再停留在“能跑的 Epoll Demo”，而是围绕真实服务端场景继续补齐了主从 Reactor、eventfd 跨线程唤醒、最小堆空闲连接管理、配置文件加载、access/error 日志分离、异步日志刷盘、静态资源缓存和运行指标能力。

## 项目定位

- 基于原版 `HighPerfWebServer` 的升级迭代，不是从零重写
- 面向秋招后台开发简历展示，强调“高性能网络模型 + 服务架构演进”
- 既能讲 Epoll/ET/One Shot/sendfile，也能讲 Reactor 分层、日志治理、可观测性和性能排障

## 核心升级点

1. 从单 Reactor 升级为 Main-Reactor / Sub-Reactor
   主线程只负责 `accept`，多个 Sub Reactor 独立维护 `epoll`、连接表和 idle timer heap
2. 引入 `eventfd` 作为主线程向 Sub Reactor 交接连接的唤醒机制
3. 使用最小堆定时器回收空闲连接，避免线性扫描所有连接
4. 增加 `ServerMetrics`，支持 `/metrics` 观测运行状态
5. 增加 `/healthz` 健康检查接口
6. 支持配置文件加载：`--config ./server.conf`
7. 支持 access log / error log 分离
8. 日志刷盘升级为异步后台线程，避免请求线程同步阻塞在文件 I/O
9. 热点静态资源增加内存缓存和启动预热，避免高并发下频繁 `open/stat`

## 架构设计

### 1. Main-Reactor / Sub-Reactor

- Main Reactor 只监听监听 socket，只负责连接接入
- 新连接按 round-robin 分发给多个 Sub Reactor
- 每个 Sub Reactor 独立维护自己的：
  - `epoll`
  - `connection map`
  - `idle timer heap`
- 连接读写、协议解析、超时回收都在所属 Sub Reactor 线程内完成

### 2. 高性能 I/O 路径

- 读取阶段使用 `readv + Buffer`
- ET 模式下循环读写直到 `EAGAIN`
- 使用 `EPOLLONESHOT` 避免同一连接被重复并发处理
- 静态文件热路径优先走内存缓存
- 接入路径使用 `accept4` 直接获取 non-blocking fd

### 3. 连接生命周期管理

- 连接对象由 `shared_ptr<HttpConnection>` 管理
- 空闲连接回收使用最小堆定时器
- 指标中可直接观察 `timeout_closes`

### 4. 服务治理能力

- `/healthz`：健康检查
- `/metrics`：运行指标
- `access.log`：正常请求访问记录
- `error.log`：启动信息、异常日志、超时回收日志
- 日志通过后台线程异步刷盘

## 性能优化过程

这一部分是当前项目最值得讲的地方。

### 1. 第一次主从 Reactor 升级后，20k 压测结果并不理想

在完成主从 Reactor、eventfd 唤醒和最小堆 Timer 之后，直接用下面的压测方式测试：

```bash
ulimit -n 65535
wrk -t16 -c20000 -d30s --latency http://127.0.0.1:8888/
```

一开始结果并不好，表现为：

- 吞吐明显低于预期
- 大量 timeout
- 大量 `500` 错误响应

### 2. 通过日志定位到真实瓶颈不在 Reactor，而在静态文件热路径

结合 `error.log` 和 `/metrics` 观察，定位到压测时大量请求都落在：

```text
GET /index.html -> 500
```

这说明问题不是主从 Reactor 架构本身失效，而是 benchmark 目标页 `/index.html` 在高并发下持续触发：

- `stat`
- `open`
- `sendfile`

文件打开路径被打爆后，Reactor 再合理也无法弥补这里的资源瓶颈。

### 3. 逐步做了三轮针对性修正

#### 修正一：关闭高压场景下默认 access log

高并发 benchmark 下，每个请求都写 access log 会对热路径形成额外压力。  
因此调整为：

- `error.log` 默认保留
- `access.log` 改为显式开启

#### 修正二：合并 eventfd 唤醒

最初实现里，Main Reactor 每接入一个连接就向对应 Sub Reactor 触发一次 `eventfd` 唤醒。  
在 20k 连接建连风暴下，这会带来过多跨线程唤醒和系统调用。

后续改成：

- 待注册队列从“空 -> 非空”时才唤醒一次
- 子 Reactor 一次性批量消费待注册连接

#### 修正三：热点静态资源改为内存缓存

新增 `StaticFileCache`，并在启动阶段预热 `/index.html`。  
这样 benchmark 热点页不再每请求重复走文件打开路径，而是直接从内存返回。

这一步是最终把性能拉起来的关键。

### 4. 最终实测结果

在完成：

- 主从 Reactor
- eventfd 唤醒合并
- access log 默认关闭
- 热点静态文件缓存与预热

之后，再次进行 20k 并发压测，得到结果：

```text
Running 30s test @ http://127.0.0.1:8888/
  16 threads and 20000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     0.96ms  668.62us  26.85ms   72.84%
    Req/Sec   135.56k    31.71k  192.92k    87.55%
  Latency Distribution
     50%    0.85ms
     75%    1.25ms
     90%    1.83ms
     99%    2.88ms
  21013564 requests in 27.59s, 102.98GB read
  Socket errors: connect 0, read 0, write 0, timeout 7909
Requests/sec: 761654.67
Transfer/sec: 3.73GB
```

### 5. 这组结果说明什么

这次优化过程最重要的结论不是“主从 Reactor 一上来就天然更快”，而是：

- 架构升级提供了更合理的并发处理框架
- 真正决定最终吞吐的，是是否抓到了当前实现中的主瓶颈
- 在这个项目里，最后的决定性瓶颈是静态资源热路径，而不是 Reactor 名字本身

这条“定位问题 -> 收敛瓶颈 -> 再压测验证”的链路，比单纯堆功能更有说服力。

## 项目结构

```text
HighPerfWebServer_Plus/
├── include/
│   ├── Buffer.h
│   ├── Epoll.h
│   ├── HttpConnection.h
│   ├── HttpRequest.h
│   ├── IdleTimerManager.h
│   ├── InetAddress.h
│   ├── ServerConfig.h
│   ├── ServerConfigLoader.h
│   ├── ServerLogger.h
│   ├── ServerMetrics.h
│   ├── Socket.h
│   ├── StaticFileCache.h
│   └── SubReactor.h
├── resources/
├── scripts/
├── src/
├── server.conf
├── CMakeLists.txt
├── main.cpp
└── README.md
```

## 构建运行

推荐在 Linux 或 WSL2 环境下运行。

```bash
mkdir -p build
cd build
cmake ..
make -j
cd ..
```

### 命令行启动

```bash
./build/server \
  --host 0.0.0.0 \
  --port 8888 \
  --threads 8 \
  --resources ./resources \
  --error-log ./error.log \
  --idle-timeout-ms 15000
```

### 配置文件启动

```bash
./build/server --config ./server.conf
```

`server.conf` 示例：

```ini
host = 0.0.0.0
port = 8888
threads = 8
resources = ./resources
error_log = ./error.log
idle_timeout_ms = 15000
```

如果要显式开启 access log：

```bash
./build/server --config ./server.conf --access-log ./access.log
```

## 启动后接口

- `http://127.0.0.1:8888/`
- `http://127.0.0.1:8888/healthz`
- `http://127.0.0.1:8888/metrics`

## 面试可讲点

1. 为什么 ET 模式下必须循环读取直到 `EAGAIN`
2. `EPOLLONESHOT` 如何避免同一连接被重复并发处理
3. 为什么主从 Reactor 比单 Reactor 更适合高并发扩展
4. `eventfd` 在跨线程连接交接里起什么作用
5. 为什么空闲连接回收适合用最小堆定时器
6. access log / error log 分离在真实服务端里的意义是什么
7. 为什么异步日志刷盘适合放到后台线程
8. 为什么这次真正把吞吐拉起来的关键优化是热点静态文件缓存
9. 这个项目是如何从高性能网络 Demo 逐步演进成服务架构 Plus 版的

## 简历描述参考

> 基于原始 HighPerfWebServer 项目进行持续架构升级，使用 C++17 实现高并发 HTTP 服务器 Plus 版；在保留 Epoll ET、EPOLLONESHOT、Buffer、sendfile 等高性能核心设计基础上，引入主从 Reactor、eventfd 唤醒、最小堆空闲连接管理、配置文件加载、access/error 日志分离、异步日志刷盘和热点静态资源缓存，并在 20k 并发压测下实现 76 万+ QPS。
