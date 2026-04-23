# HighPerfWebServer_Plus

`HighPerfWebServer_Plus` 是我基于原始项目 [HighPerfWebServer](https://github.com/chengyebi/HighPerfWebServer) 做完整服务架构扩充与工程化升级后的高并发服务器 Plus 版。

它不再只是一个“能跑起来的 Epoll Demo”，而是一个更适合秋招简历展示的迭代升级项目：在保留 `Epoll ET + EPOLLONESHOT + ThreadPool + Buffer + sendfile` 这些高性能核心设计的基础上，继续补齐了配置化启动、运行时指标、健康检查、静态资源服务增强、优雅退出和项目展示页面等能力。

## 项目定位

这个版本的定位是：

- 基于原版 `HighPerfWebServer` 的二次升级，而不是从零重写
- 面向秋招简历展示，强调“从高性能网络模型到完整服务架构”的迭代思路
- 既能讲底层 I/O 多路复用、线程池、零拷贝，也能讲服务治理和工程化能力

## 相比原版的升级点

1. 新增服务配置模块，支持命令行启动参数：
   `--host`、`--port`、`--threads`、`--resources`
2. 新增运行指标模块 `ServerMetrics`：
   支持统计连接数、请求数、响应数、错误数、超时关闭次数、读写字节数、服务运行时长
3. 新增内置服务接口：
   `GET /healthz` 用于健康检查，`GET /metrics` 用于观测运行状态
4. 增强静态资源响应能力：
   支持 `GET / HEAD`，支持常见 MIME 类型识别，不再只返回单一 HTML
5. 优化服务生命周期管理：
   增加 `SIGINT / SIGTERM` 优雅退出处理，便于本地演示和压测
6. 新增线程安全日志模块：
   支持启动日志、访问日志、异常日志和空闲连接回收日志输出
7. 增加空闲连接回收能力：
   支持通过 `--idle-timeout-ms` 配置空闲超时时间，避免长连接长期占用 fd
8. 将空闲连接管理从主循环全量扫描升级为最小堆定时器模型：
   通过堆顶到期时间驱动回收，降低超时检测开销，更贴近生产服务器实现
9. 升级项目展示层：
   首页从简单 Hello 页面升级为可直接用于面试演示的项目介绍页
10. 补充工程化细节：
   构建目录忽略、CMake 配置整理、README 重写，仓库结构更适合公开展示

## 核心架构

### 1. 高并发事件驱动模型

- 主线程负责 `accept` 新连接并分发事件
- 使用 `epoll` 边沿触发模式减少重复通知
- 使用 `EPOLLONESHOT` 避免同一连接被多个 worker 并发处理
- 工作线程池负责具体连接读写和协议处理

### 2. 高性能数据收发

- 读取阶段使用 `readv + Buffer`，适配 ET 模式下尽可能多地搬运数据
- 发送静态文件时使用 `sendfile`，减少用户态与内核态之间的数据拷贝
- 输出缓冲区与文件发送路径分离，兼顾小响应和大文件响应

### 3. 连接生命周期与 ABA 风险规避

- 连接对象由 `shared_ptr<HttpConnection>` 管理
- 延迟清理阶段使用 `{fd, weak_ptr<HttpConnection>}` 做匹配
- 避免旧 fd 关闭后被复用，导致错误删除新连接的 ABA 问题

### 4. 服务治理能力补充

- `/healthz` 用于服务健康检查
- `/metrics` 用于压测时观察服务运行状态
- 启动参数配置化，适合不同线程数和资源目录的实验
- 日志系统支持记录服务启动、请求访问和异常事件
- 空闲连接回收机制避免 keep-alive 连接长期占用资源
- 使用最小堆定时器维护超时连接，而不是主线程每轮线性扫描所有连接

## 项目结构

```text
HighPerfWebServer_Plus/
├── include/
│   ├── Buffer.h
│   ├── Epoll.h
│   ├── HttpConnection.h
│   ├── HttpRequest.h
│   ├── InetAddress.h
│   ├── ServerConfig.h
│   ├── ServerMetrics.h
│   ├── Socket.h
│   └── ThreadPool.h
├── resources/
├── src/
├── CMakeLists.txt
├── main.cpp
└── README.md
```

## 构建运行

推荐在 Linux 或 WSL2 环境下运行：

```bash
mkdir -p build
cd build
cmake ..
make -j
cd ..

./build/server --host 0.0.0.0 --port 8888 --threads 8 --resources ./resources

# 可选：开启日志文件并配置空闲连接超时
./build/server --host 0.0.0.0 --port 8888 --threads 8 --resources ./resources --log ./server.log --idle-timeout-ms 15000
```

启动后可访问：

- `http://127.0.0.1:8888/`
- `http://127.0.0.1:8888/healthz`
- `http://127.0.0.1:8888/metrics`

## 压测示例

```bash
ulimit -n 65535
wrk -t8 -c5000 -d15s --latency http://127.0.0.1:8888/
```

我在当前环境下做过一轮保守压测，受 WSL 文件描述符硬限制影响，无法直接达到 `-c20000`，但在当前限制内已经完成了高并发压力验证。

## 面试可讲点

1. 为什么 ET 模式下必须循环读取直到 `EAGAIN`
2. `EPOLLONESHOT` 如何解决多线程 Reactor 的重复处理问题
3. 为什么静态文件发送选择 `sendfile`
4. 如何避免 fd 复用引起的 ABA 清理问题
5. 为什么一个高并发服务器还需要 `/metrics` 和 `/healthz`
6. 空闲连接为什么要做超时回收，它对 fd 资源和高并发稳定性有什么意义
7. 为什么空闲连接回收更适合用最小堆定时器，而不是每轮遍历所有连接
8. 这个项目是如何从“高性能网络 Demo”演进成“完整服务架构 Plus 版”的

## 后续可继续扩展

- 定时器与空闲连接回收
- access log / error log 拆分
- 异步日志刷盘
- HTTP 请求体与 Chunked 解析补全
- 主从 Reactor 架构升级
- Benchmark 脚本自动化
- 配置文件加载与热更新

## 简历描述参考

> 基于原始 HighPerfWebServer 项目进行二次架构升级，使用 C++17 实现高并发 HTTP 服务器 Plus 版；在保留 Epoll ET、EPOLLONESHOT、线程池、Buffer、sendfile 等高性能核心设计的基础上，新增服务配置模块、健康检查、运行指标统计、静态资源服务增强与优雅退出能力，提升项目工程化与服务化程度。
