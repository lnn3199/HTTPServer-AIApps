# Kama-HTTPServer

基于 **Muduo** 的 C++17 HTTP 服务框架：事件驱动、路由与中间件、会话、MySQL 连接池，以及基于 **OpenSSL** 的可选 **HTTPS**（`SslConfig` / `SslContext` / `SslConnection`）。  
仓库内附带示例应用 **GomokuServer**（在线五子棋），演示如何在本框架上挂业务。

---

## 技术栈与依赖

| 项 | 说明 |
|----|------|
| 语言 / 构建 | C++17，`CMake` ≥ 3.10 |
| 网络 | Muduo（`muduo_net`、`muduo_base`） |
| TLS | OpenSSL（`ssl`、`crypto`），服务端 TLS 由 `TLS_server_method` 等与 `SSL_CTX` 配置 |
| 数据库 | MySQL Connector/C++（`mysqlcppconn` / `mysqlcppconn8`）、`mysqlclient` |

---

## 仓库结构（与代码一致）

```
HttpServer/
  include|src/http/          # HttpRequest / HttpResponse / HttpContext / HttpServer
  include|src/router/       # 路由与 Handler
  include|src/session/       # 会话（Session、SessionManager、存储实现）
  include|src/middleware/    # 中间件链、CORS 等
  include|src/utils/         # MysqlUtil、JsonUtil、FileUtil；utils/db 连接池
  include|src/ssl/           # SslConfig、SslContext、SslConnection
WebApps/GomokuServer/        # 示例：路由 Handler、GomokuServer、main
CMakeLists.txt               # 汇总 HttpServer 与 Gomoku 源码，生成可执行文件 simple_server
```

---

## 请求处理流程（启用 HTTPS 时）

```
客户端
  → TcpConnection 收字节
  → HttpServer::onMessage
       （若 useSSL_：SslConnection::onRead 解密后再交给 HTTP 解析）
  → HttpContext::parseRequest → HttpRequest
  → HttpServer::onRequest
  → handleRequest：MiddlewareChain::processBefore → Router::route → processAfter
  → HttpResponse::appendToBuffer
  → sendToClient（明文走 TcpConnection::send；HTTPS 走 SslConnection::send 加密）
  → （短连接等场景）conn->shutdown
```

**HTTP 解析约定：** 当前实现**不处理 HTTP/1.1 pipelining**（同一 keep-alive 连接上、在收到上一响应前连续发送多个请求，且可能一次读入多段完整请求）。默认假设客户端为「一次请求 → 等响应 → 再发下一请求」的常见用法；若需支持管线化，需在消息回调中对同一缓冲在 `gotAll` / `reset` 后循环调用 `parseRequest`。

---

## 编译

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

可执行文件默认名为 **`simple_server`**（见根目录 `CMakeLists.txt`）。  

需本机已安装 **Muduo**、**OpenSSL**、**MySQL 开发包与 Connector/C++**；`CMakeLists.txt` 中 MySQL 头路径以常见 Linux 路径为例，若环境不同请自行调整 `include_directories` / `find_library`。

---

## 模块说明（精简）

- **HTTP 核心**：解析请求、构造响应、在 `TcpServer` 上注册连接与消息回调。
- **路由**：按方法与路径分发到回调或 `RouterHandler`；支持正则等扩展（见 `Router`）。
- **会话**：`SessionManager` + 存储抽象（如内存实现），供登录态等使用。
- **中间件**：`MiddlewareChain` 在路由前后插入逻辑；内置 **CORS** 示例。
- **数据库**：`DbConnectionPool` 单例 + `DbConnection`；`MysqlUtil` 提供静态 `init` / `executeQuery` / `executeUpdate` 入口。
- **HTTPS**：`HttpServer` 在构造时可选 `useSSL`，通过 `setSslConfig` 加载证书后 `SslContext::initialize`；每条连接对应 `SslConnection`（内存 BIO + 握手与应用数据解密）。

---

## 说明

- **HTTP/1.1 pipelining**：见上文「HTTP 解析约定」，本仓库默认不实现。
- 示例 **GomokuServer** 仅作开发演示，业务可替换为任意路由与 Handler。
- `SslConfig` 中部分字段（如客户端证书校验相关）若未在 `SslContext` 中接线，则仅作配置占位，不影响当前单向 HTTPS 主路径。
