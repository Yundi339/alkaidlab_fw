# alkaidlab_fw

轻量级 C++11 HTTP 服务端框架，基于 libhv 封装。

## 快速开始

```bash
# 克隆仓库
git clone --recursive git@github.com:Yundi339/alkaidlab_fw.git
cd alkaidlab_fw

# 拉取子模块
git submodule update --init --recursive

# 构建
./build.sh
```

## 为什么做这个框架

现有 C++ HTTP 框架普遍要求 C++14/17 甚至 C++20，对老旧生产环境（CentOS 7 等）不友好。alkaidlab_fw 的核心设计目标是 **最小兼容 C++11**，在保持现代框架风格（洋葱模型中间件、声明式路由、异步支持）的同时，确保可以在 GCC 4.8.5+ 环境中编译和运行。

## 模块功能

框架分为四大模块：**核心**、**工具**、**并发**、**网络**。

```
alkaidlab_fw/
├── 核心模块 ─────── Application / Router / Context / MiddlewareChain / IniConfig
├── 工具模块 ─────── JWT / Hash / Password / Base64 / PathGuard / Logger / Id / Time / Json / Cert
├── 并发模块 ─────── LockfreeQueue / SPSCQueue / AtomicCounter / FlowController / ThreadPool
└── 网络模块 ─────── libhv HTTP/1.1 传输
```

### 核心模块

| 组件 | 职责 |
|------|------|
| `fw::Application` | HTTP 服务器生命周期管理（路由挂载、中间件、SSL、异步） |
| `fw::Context` | 请求/响应封装 + KV 中间件数据传递 |
| `fw::Router` | 统一路由注册（GET/POST/PUT/DELETE/PATCH + 异步） |
| `fw::MiddlewareChain` | 洋葱模型中间件链 |
| `fw::IniConfig` | INI 配置文件解析 |

### 工具模块

| 模块 | 说明 |
|------|------|
| `Base64` | Base64 编解码 |
| `HashUtil` | SHA-256 等哈希计算 |
| `JwtUtil` | JWT 令牌生成与验证 |
| `PasswordUtil` | 密码哈希与校验 |
| `CertUtil` | SSL 证书工具 |
| `IdUtil` | UUID / Snowflake ID 生成 |
| `TimeUtil` | 时间格式化与解析 |
| `PathGuard` | 路径安全校验（防遍历攻击） |
| `JsonUtil` | JSON 序列化辅助 |
| `Logger` / `LogConfig` | 日志文件 / 级别 / 轮转配置 |

### 并发组件

| 模块 | 说明 |
|------|------|
| `AtomicCounter` | 无锁原子计数器 |
| `FlowController` | 流量控制 |
| `LockfreeQueue` | 多生产者多消费者无锁队列 |
| `SPSCQueue` | 单生产者单消费者无锁队列 |
| `SupervisedThreadPool` | 受监督线程池 |

## 用法

### 独立构建

```bash
# 构建（自动 clone vcpkg 并安装依赖）
./build.sh

# 复用已有 vcpkg（推荐，避免重复下载）
./build.sh --vcpkg-root /path/to/vcpkg

# 指定安装目录
./build.sh --install-dir /path/to/output

# 构建并运行测试
./build.sh --test

# 清空后重新构建
./build.sh --clean
```

### 作为子模块集成

```bash
# 在你的项目中添加子模块
git submodule add git@github.com:Yundi339/alkaidlab_fw.git third_party/alkaidlab_fw
git submodule update --init --recursive

# 构建（复用项目的 vcpkg）
bash third_party/alkaidlab_fw/build.sh \
    --vcpkg-root "$PWD/vcpkg" \
    --install-dir "$PWD/build_cache/alkaidlab_fw_install"
```

### 代码示例

```cpp
#include "fw/Application.hpp"
#include "fw/Router.hpp"
#include "fw/HttpConstants.hpp"

int main() {
    fw::Application app;
    fw::Router router;

    router.use("logger", [](auto& ctx, auto next) {
        return next();
    });

    router.get("/hello", [](fw::Context& c) {
        c.json(200, R"({"msg":"hello"})");
    });

    app.mount(router);
    app.setHost("0.0.0.0");
    app.setPort(8080);
    app.setWorkerThreads(4);
    app.start();
    // ... wait for shutdown ...
    app.stop();
}
```

## 构建依赖

- **C++11**（GCC 4.8.5+ / Clang 3.4+）
- CMake 3.14+

运行时依赖通过 vcpkg 自动管理（`build.sh` 自动处理），无需手动安装。

## 测试

8 个测试套件，覆盖 Context（40 case）/ Middleware / Router / Base64 / LogConfig / IniConfig / HttpConstants / Application。

## 鸣谢

依赖开源项目：

- **[libhv](https://github.com/ithewei/libhv)** — 高性能跨平台网络库，提供 HTTP 服务器核心传输能力
- **[Boost](https://www.boost.org/)** — 线程、文件系统、UUID 等基础设施
- **[OpenSSL](https://www.openssl.org/)** — TLS/SSL 加密与证书处理
- **[spdlog](https://github.com/gabime/spdlog)** — 高性能日志库
- **[nlohmann/json](https://github.com/nlohmann/json)** — 现代 C++ JSON 库
- **[fmt](https://github.com/fmtlib/fmt)** — 格式化库
## 许可

[BSD 3-Clause License](LICENSE)
