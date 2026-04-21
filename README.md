# HTTPServer-AIApps

顶层 **CMake 工程名**为 **`HTTPServer-AIApps`**（与源码目录 **`HttpServer/`** 不同：后者指框架库代码目录）。

基于 **Muduo** 的 C++17 HTTP 服务框架：事件驱动、路由与中间件、会话、MySQL 连接池，以及基于 **OpenSSL** 的可选 **HTTPS**（`SslConfig` / `SslContext` / `SslConnection`）。

**`HttpServer/` 为框架实现**；本仓库默认挂载的业务应用为 **`AIApps/ChatServer/`**（多模型对话、工具注册、RAG、语音、图像识别、RabbitMQ 异步入库等），与框架解耦，可按同样方式替换为其它业务。

默认可执行文件：**`http_server`**（`HttpServer` + `AIApps/ChatServer`）。

---

## 仓库结构

```
HttpServer/
  include|src/http/          # HttpRequest / HttpResponse / HttpContext / HttpServer
  include|src/router/        # 路由与 Handler
  include|src/session/       # 会话（Session、SessionManager、存储实现）
  include|src/middleware/    # 中间件链、CORS 等
  include|src/utils/         # MysqlUtil、JsonUtil、FileUtil；utils/db 连接池
  include|src/ssl/           # SslConfig、SslContext、SslConnection
AIApps/ChatServer/           # 业务：ChatServer、handlers、AIUtil、resource（页面与 config.json）
  src/utils/PasswordHash.cpp # 用户密码：PBKDF2-HMAC-SHA256（注册与登录校验）
CMakeLists.txt               # 生成 http_server
```

---

## 技术栈与依赖

### 框架与运行时（`HttpServer` + 业务共用）

| 项 | 说明 |
|----|------|
| 语言 / 构建 | C++17，`CMake` ≥ 3.10 |
| 网络 | Muduo（`muduo_net`、`muduo_base`） |
| TLS | OpenSSL（`ssl`、`crypto`） |
| 数据库 | MySQL Connector/C++（`mysqlcppconn` / `mysqlcppconn8`）、`mysqlclient` |

### 当前 AI 业务额外依赖

| 项 | 说明 |
|----|------|
| HTTP 客户端 | **libcurl**（大模型 / 语音等 HTTP 调用） |
| 图像 | **OpenCV**（含 `dnn` 等组件） |
| 推理 | **ONNX Runtime**（`libonnxruntime`） |
| 消息队列 | **RabbitMQ C 库** `rabbitmq`、**SimpleAmqpClient**（异步入库） |

CMake 中 MySQL 头路径以常见 Linux 路径为例；若环境不同请修改 `CMakeLists.txt` 中的 `MYSQL_INCLUDES` 等。

---

## 认证与密码存储（Chat）

- 新注册用户：密码以 **PBKDF2-HMAC-SHA256**（高迭代次数 + 随机盐）写入 `users.password` 字段，格式见 `AIApps/ChatServer/src/utils/PasswordHash.cpp`。
- 旧数据若为 `sha256$...`，登录校验成功后会 **自动升级** 为 PBKDF2；**不再接受数据库中的明文密码**。
- 传输层安全依赖部署侧 **HTTPS** 与网络隔离；应用内 MySQL 连接参数请按环境配置，避免将生产口令硬编码进仓库。

---

## 静态资源与前端页面

业务静态页与配置均在 **`AIApps/ChatServer/resource/`**：

| 文件 | 作用 |
|------|------|
| `entry.html` | 登录 / 注册（内联 CSS，无单独配图文件） |
| `menu.html` | 进入各子服务入口 |
| `AI.html` | AI 对话界面 |
| `upload.html` | 图像识别上传页 |
| `config.json` | 业务与模型等配置 |
| `schema.sql` | 示例库表结构 |

**关于「AI 登录配图」**：登录界面由 `entry.html` 的样式与布局直接渲染，**工程内没有单独的「登录插图」图片路径**；若需要品牌图或背景图，可将资源放在 `resource/`（或子目录）并在该 HTML 中增加 `<img>` 或 `background-image` 引用。

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

产物：**`http_server`**。

---

## 业务应用（`AIApps/ChatServer`）

- 入口：`AIApps/ChatServer/src/main.cpp`（HTTP 服务、初始化聊天消息、RabbitMQ 消费线程等）。
- 静态与配置：`AIApps/ChatServer/resource/`（含 `config.json`、页面等）；数据库与连接信息在 `ChatServer.cpp` 等处按环境修改。
- 运行前需准备：**MySQL**、**RabbitMQ**（与代码中队列名一致），以及各云 API / 密钥（按配置填写）。

业务相关逻辑均在 **`AIApps/ChatServer/`**；**HTTP 框架以 `HttpServer/` 为准**。

---

## 模块说明（框架，精简）

- **HTTP 核心**：解析请求、构造响应、在 `TcpServer` 上注册连接与消息回调。
- **路由**：按方法与路径分发到回调或 `RouterHandler`；支持正则等扩展（见 `Router`）。
- **会话**：`SessionManager` + 存储抽象（如内存实现），供登录态等使用。
- **中间件**：`MiddlewareChain` 在路由前后插入逻辑；内置 **CORS** 示例。
- **数据库**：`DbConnectionPool` 单例 + `DbConnection`；`MysqlUtil` 提供静态 `init` / `executeQuery` / `executeUpdate` 入口。
- **HTTPS**：`HttpServer` 在构造时可选 `useSSL`，通过 `setSslConfig` 加载证书后 `SslContext::initialize`；每条连接对应 `SslConnection`（内存 BIO + 握手与应用数据解密）。

---

## 说明

- **HTTP/1.1 pipelining**：见上文「HTTP 解析约定」，本仓库默认不实现。
- **`AIApps/ChatServer`** 为当前默认业务示例，可替换为任意路由与 Handler。
- `SslConfig` 中部分字段（如客户端证书校验相关）若未在 `SslContext` 中接线，则仅作配置占位，不影响当前单向 HTTPS 主路径。
