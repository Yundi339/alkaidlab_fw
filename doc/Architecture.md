# alkaidlab_fw 架构说明

C++11 HTTP 服务端框架抽象层，解耦业务逻辑与 libhv。

## 模块分类

| 分类 | 组件 | 头文件 |
|------|------|--------|
| **核心框架** | Application | fw/Application.hpp — 服务器生命周期（路由+中间件+SSL+异步） |
| | Context | fw/Context.hpp — 请求/响应封装 + KV 数据传递（pimpl 隔离 libhv） |
| | MiddlewareChain | fw/Middleware.hpp — 洋葱模型中间件链 |
| | Router | fw/Router.hpp — 统一路由注册 + libhv 桥接 |
| | HttpConstants | fw/HttpConstants.hpp — HTTP 状态码/方法枚举 |
| **传输层** | ServerTransport | fw/ServerTransport.hpp — 传输抽象接口 |
| | HvTransport | fw/HvTransport.hpp — libhv HTTP/1.1 传输实现 |
| | HttpTransport | fw/HttpTransport.hpp — HTTP 客户端传输 |
| | HttpsTransport | fw/HttpsTransport.hpp — HTTPS 客户端传输 |
| | TcpTransport | fw/TcpTransport.hpp — TCP 传输 |
| | WebSocketTransport | fw/WebSocketTransport.hpp — WebSocket 传输 |
| | TransportFactory | fw/TransportFactory.hpp — 传输工厂 |
| **无锁并发** | LockfreeQueue | fw/LockfreeQueue.hpp — MPMC 无锁队列（boost::lockfree） |
| | SPSCQueue | fw/SPSCQueue.hpp — SPSC 单产单消队列 |
| | AtomicCounter | fw/AtomicCounter.hpp — 原子计数器 |
| | FlowController | fw/FlowController.hpp — 队列流控（防溢出+丢包统计） |
| **线程池** | SupervisedThreadPool | fw/SupervisedThreadPool.hpp — 受监督线程池（worker 异常自动恢复） |
| **工具** | Logger | fw/Logger.hpp — spdlog 封装（控制台+文件） |
| | PathGuard | fw/PathGuard.hpp — 路径安全校验（系统目录黑名单） |
| | JwtUtil | fw/JwtUtil.hpp — JWT HMAC-SHA256 |
| | HashUtil | fw/HashUtil.hpp — SHA256/MD5 哈希 |
| | PasswordUtil | fw/PasswordUtil.hpp — bcrypt 密码哈希 |
| | IdUtil | fw/IdUtil.hpp — 雪花算法 ID |
| | Base64 | fw/Base64.hpp — Base64 编解码 |
| | JsonUtil | fw/JsonUtil.hpp — nlohmann::json 封装 |
| | TimeUtil | fw/TimeUtil.hpp — 时间格式化/解析 |
| | CertUtil | fw/CertUtil.hpp — SSL/TLS 证书工具 |
| | IniConfig | fw/IniConfig.hpp — INI 配置解析（opaque pointer） |
| | LogConfig | fw/LogConfig.hpp — 日志配置（文件/级别/轮转） |

## 依赖

| 依赖 | 用途 |
|------|------|
| libhv | HTTP 服务器核心（git 子模块，build_cache/libhv_install/） |
| Boost | thread/chrono/system/filesystem/random/uuid/lockfree |
| OpenSSL | HTTPS + HMAC-SHA256 |
| spdlog | 日志 |
| nlohmann-json | JSON |
| GTest | 测试（可选） |

## 构建

```bash
bash build.sh                           # 构建+安装到上级 build_cache/alkaidlab_fw_install/
bash build.sh --test                    # 构建+测试
bash build.sh --clean                   # 清空重建
bash build.sh --vcpkg-root /path/vcpkg  # 指定 vcpkg 根目录
bash build.sh --install-dir /path       # 指定安装路径
```

产物：`<install-dir>/lib/libalkaidlab_fw.a` + `<install-dir>/include/fw/` + `<install-dir>/lib/cmake/alkaidlab_fw/alkaidlab_fw-config.cmake`

### 修改 libhv 源码后强制重建

libhv 走**缓存式构建**：build.sh 检测到 `build_cache/libhv_install/lib/libhv_static.a` 存在就跳过编译。`--clean` 仅清 fw 自身的 build 目录，**不清 libhv 缓存**。

修改 libhv 源码（cherry-pick、子模块更新等）后必须手动清理：

```bash
# 在项目根执行
rm -rf third_party/alkaidlab_fw/build_cache/libhv_install/   # libhv 安装产物（关键）
rm -rf third_party/alkaidlab_fw/third_party/libhv/build/     # libhv 编译中间产物
rm -rf third_party/alkaidlab_fw/build/                       # fw 编译目录（需重链）
rm -rf build_cache/alkaidlab_fw_install/                     # fw 已安装产物
find build/ -mindepth 1 -maxdepth 1 ! -name 'vcpkg_installed' -exec rm -rf {} +  # 主工程
bash deploy/dev_install.sh --backend-only
```

## 设计决策

1. **pimpl 隔离**：Context.hpp 零 libhv `#include`，业务层无需接触 libhv 类型
2. **KV 替代 X-Internal-\* Header**：中间件数据传递用 `set()`/`get()`，不污染 HTTP header
3. **洋葱模型**：`int(Context&, Next)` 签名，`next()` 进入下层，不调用则中断
4. **shared_ptr 共享中间件链**：Router::bind() 创建一份 chain 所有路由共享
5. **C++11 兼容**：不使用 C++14/17 特性

## 单元测试

20 个测试文件，覆盖核心框架（Context/Middleware/Router/Application） + 工具 + 无锁并发。
