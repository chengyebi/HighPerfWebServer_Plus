# HighPerfWebServer_Plus

`HighPerfWebServer_Plus` 是基于原始项目 [HighPerfWebServer](https://github.com/chengyebi/HighPerfWebServer) 持续迭代出的高并发服务器 Plus 版。

这个版本的目标不是停留在“能跑的 Epoll Demo”，而是继续补齐更完整的服务架构能力：主从 Reactor、最小堆空闲连接管理、配置化启动、access log / error log 分离、运行指标、健康检查和更清晰的工程结构。

## 当前版本定位

- 基于原版 `HighPerfWebServer` 的升级迭代，而不是从零重写
- 面向秋招简历展示，强调“高性能网络模型 + 服务架构演进”
- 既能讲 Epoll、ET、One Shot、sendfile，也能讲 Reactor 分层、日志治理、配置加载和可观测性

## 升级点

1. 单 Reactor 升级为主从 Reactor 架构
   主线程只负责 `accept`，多个 Sub Reactor 各自维护独立 `epoll`、连接表和超时堆
2. 新增配置文件加载能力
   支持 `--config ./server.conf`，使用轻量 `key=value` 配置格式
3. 支持 access log / error log 分离
   正常请求流量和异常事件分开记录，更接近真实服务端部署方式
4. 日志刷盘升级为异步后台线程
   请求线程只负责入队，后台线程批量写入日志文件，降低同步 I/O 对热路径的影响
5. 新增运行指标模块
   支持统计连接数、请求数、响应数、错误数、超时关闭次数、读写字节数、服务运行时长
6. 新增内置接口
   `GET /healthz` 用于健康检查，`GET /metrics` 用于观测运行状态
7. 增强静态资源能力
   支持 `GET / HEAD`，支持常见 MIME 类型识别
8. 增加优雅退出和空闲连接回收
   支持 `SIGINT / SIGTERM`，空闲连接使用最小堆定时器回收
9. 接入路径优化
   使用 `accept4` 直接获取 non-blocking fd，减少额外系统调用

## 核心架构

### 1. Main-Reactor / Sub-Reactor

- Main Reactor 只监听监听 socket，并负责连接接入
- 新连接按 round-robin 分发给多个 Sub Reactor
- 每个 Sub Reactor 独立维护自己的 `epoll`、连接表和 idle timer heap
- 连接读写、协议解析、超时回收都在所属 Sub Reactor 线程内完成

相比单 Reactor，这种结构能减少主线程热点，更适合连接数继续上升时扩展。

### 2. 高性能 I/O 路径

- 读取阶段使用 `readv + Buffer`
- 静态文件发送使用 `sendfile`
- ET 模式下循环读写直到 `EAGAIN`
- `EPOLLONESHOT` 避免同一连接被多个执行流重复处理

### 3. 连接生命周期管理

- 连接对象由 `shared_ptr<HttpConnection>` 管理
- 空闲连接回收使用最小堆定时器，而不是每轮线性扫描全部连接
- 指标里可直接看到 `timeout_closes`

### 4. 服务治理能力

- `/healthz`：健康检查
- `/metrics`：运行指标
- `access.log`：正常请求访问记录
- `error.log`：启动信息、异常日志、超时回收日志
- 日志由后台线程异步刷盘，减少请求线程直接阻塞在文件 I/O 上

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

### 方式 1：直接命令行启动

```bash
./build/server \
  --host 0.0.0.0 \
  --port 8888 \
  --threads 8 \
  --resources ./resources \
  --access-log ./access.log \
  --error-log ./error.log \
  --idle-timeout-ms 15000
```

### 方式 2：配置文件启动

```bash
./build/server --config ./server.conf
```

`server.conf` 示例：

```ini
host = 0.0.0.0
port = 8888
threads = 8
resources = ./resources
access_log = ./access.log
error_log = ./error.log
idle_timeout_ms = 15000
```

## 启动后接口

- `http://127.0.0.1:8888/`
- `http://127.0.0.1:8888/healthz`
- `http://127.0.0.1:8888/metrics`

## 压测说明

```bash
ulimit -n 65535
wrk -t8 -c5000 -d15s --latency http://127.0.0.1:8888/
```

注意：如果运行在当前这类受限 WSL 环境中，`ulimit -n 65535` 可能不会真正生效，实际文件描述符上限仍可能只有 `1024`，这会直接限制 `wrk -c20000` 这类压测是否可执行。  
如果要做更高连接数测试，建议把项目放到 Linux 文件系统下，并先确认 `nofile` 软硬限制已经放开。

## 面试可讲点

1. 为什么 ET 模式下必须循环读取直到 `EAGAIN`
2. `EPOLLONESHOT` 如何避免同一连接被重复并发处理
3. 为什么主从 Reactor 比单 Reactor 更适合高并发扩展
4. 为什么静态文件发送适合使用 `sendfile`
5. 为什么空闲连接回收更适合用最小堆定时器而不是每轮扫描
6. access log / error log 分离在真实服务端里的意义是什么
7. 为什么日志刷盘适合做成异步后台线程
8. 为什么配置文件加载是“项目工程化”里一个重要信号
9. 这个项目是如何从高性能网络 Demo 逐步演进成服务架构 Plus 版的

## 后续可以继续扩展

- HTTP 请求体和 Chunked 完整支持
- 配置热更新
- 主从 Reactor 负载均衡策略优化
- Benchmark 自动化脚本
- 更细粒度的指标导出

## 简历描述参考

> 基于原始 HighPerfWebServer 项目进行持续架构升级，使用 C++17 实现高并发 HTTP 服务器 Plus 版；在保留 Epoll ET、EPOLLONESHOT、Buffer、sendfile 等高性能核心设计基础上，引入主从 Reactor、最小堆空闲连接管理、access/error 日志分离、配置文件加载、健康检查与运行指标模块，提升项目工程化与服务化能力。
