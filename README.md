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

**HTTP 解析约定：**

- **请求头字段名**：按 HTTP 语义 **不区分大小写**；解析后 **以小写形式存入** `HttpRequest`，`getHeader` 查找时也会将字段名转为小写。
- **请求体（POST/PUT 等）**：仅支持带 **`Content-Length`** 的请求体；**不支持** **`Transfer-Encoding: chunked`**。`Content-Length` 使用 **`std::from_chars`** 解析（非法为语法错误，不依赖 `stoi` 异常）。
- **请求体大小限制**：默认最大 **8MiB**（可用 **`HttpServer::setMaxRequestBodyBytes`** 调整，宜在监听前设置）。超过上限时返回 **413 Payload Too Large** 并关闭连接；其它解析错误多为 **400**。
- **HTTP/1.1 pipelining**：当前实现**不处理**（同一 keep-alive 连接上、在收到上一响应前连续发送多个请求，且可能一次读入多段完整请求）。默认假设客户端为「一次请求 → 等响应 → 再发下一请求」的常见用法；若需支持管线化，需在消息回调中对同一缓冲在 `gotAll` / `reset` 后循环调用 `parseRequest`。

### 响应与错误处理（框架）

- **`HttpResponse::appendToBuffer`**：会按 **`body_.size()`** 自动写入 **`Content-Length`**；`headers_` 中若也含有 `Content-Length`（任意大小写），序列化时会 **跳过** 以避免重复，**对外长度以 body 为准**。
- **`onRequest` 日志**：仅输出 **状态码、路径、序列化后总字节数**，不记录完整响应体（避免敏感信息与大 body 刷日志）。
- **`handleRequest` 异常**：返回 **500** 时对外正文为固定 **「Internal Server Error」**，**`e.what()` 只写日志**。
- **`HttpRequest::swap`**：会交换 **body（`content_`）与 `contentLength_`**，与 **`HttpContext::reset`** 配合时可完全清空上一请求残留。

---

## HTTPS / TLS（`SslConnection` 实现要点）

以下为近期在 **`HttpServer/include|src/ssl/`** 侧加强的行为，便于部署与排障。

### 连接生命周期

- **`SslConnection::ok()`**：若 **`SSL_new` 或 `BIO_new` 失败**，则 **`ssl_` 为空**。**`HttpServer::onConnection`** 在 **`ok()` 为假时不再加入 `sslConns_`**，并 **`conn->shutdown()`**，避免表内出现无法握手的半残对象。
- **析构**：先 **`setWriteCompleteCallback({})`** 再 **`SSL_free`**，避免连接晚释放时回调仍指向已销毁的 `SslConnection`。

### 入站密文（`readBio_`）

- **`feedReadBio`**：对 **`BIO_write(readBio_, …)`** 按段校验 **返回值必须等于写入长度**；失败则 **`flushWriteBio()`**、**`shutdown`**，且 **不对 Muduo 的 `buf` 做 `retrieve`**，避免「应用层已丢 TCP 字节、TLS 侧只喂了一半」的不一致。  
- 含义：**`writeBio_`（待发密文）** 在失败路径上仍会通过 **`flushWriteBio()`** 尽量发往对端后再关连。

### 出站明文（`SSL_write` 与 Muduo）

- **`SSL_write`**：循环处理 **`0 < n < len` 的部分写入**；若出现 **`SSL_ERROR_WANT_READ` / `SSL_ERROR_WANT_WRITE`**，未写完的明文追加到 **`pendingPlain_`**，由后续 **`drainPendingPlain()`** 续写。
- **`TcpConnection::setWriteCompleteCallback`**：在 **TCP 发送侧有空间** 时调用 **`onWriteComplete()`** → **`flushWriteBio()`**，握手阶段可再次 **`handleHandshake()`**，应用阶段 **`drainPendingPlain()`**，避免仅依赖 **`onRead`** 时在 **WANT_WRITE** 下卡住。
- **致命 TLS 错误**：**`send` / `drainPendingPlain`** 等对 **`SSL_write` 的不可恢复错误** 会清 **`pendingPlain_`**、置错误状态并 **关连接**。

### 部署上仍需注意（代码未替你「自动配好证书」）

- **证书链**：当前为 **`SSL_CTX_use_certificate_file`（叶子）** + 可选 **`SSL_CTX_use_certificate_chain_file`（链）**。**缺中间 CA** 时易出现 **部分客户端 / 部分网络握手失败**；部署应用 **`openssl s_client` 等工具检查完整链**。  
- **双向 TLS（mTLS）**：**`SslConfig::verifyClient_` 等未接入 `SSL_CTX_set_verify` / 信任库**，**当前仅为配置占位**；**不能**依赖其做客户端证书校验。仅 **服务端证书、客户端验服务器** 的单向 HTTPS 主路径不受影响。

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
- **路由**：按方法与路径分发；精确匹配用哈希表。**带 `/:param` 的动态路由**按 **首段参数前的静态前缀分桶**，请求时只对相关桶内条目做 `regex_match`，减少 CPU。**`Router`** 使用 **`std::shared_mutex`**：`route` 共享读锁，注册路由独占写锁，便于 **`setThreadNum > 1`** 时多 IO 线程并发查表。
- **会话**：`SessionManager` + 存储抽象（如内存实现），供登录态等使用；**`SessionManager` 与 `MemorySessionStorage`** 对共享数据结构加 **互斥锁**，避免多线程会话读写数据竞争。
- **中间件**：`MiddlewareChain` 在路由前后插入逻辑；内置 **CORS** 示例；链本身使用 **读写锁**（执行 `processBefore`/`After` 共享读，`addMiddleware` 独占写）。
- **数据库**：`DbConnectionPool` 单例 + `DbConnection`；`MysqlUtil` 提供静态 `init` / `executeQuery` / `executeUpdate` 入口。
- **HTTPS**：见 **「HTTPS / TLS（SslConnection 实现要点）」**；概要：`HttpServer` 可选 `useSSL`，**`setSslConfig`** 初始化 **`SslContext`**；每连接 **`SslConnection`**（内存 BIO）。**`setSslConfig` 返回 `bool`**，失败不 **`abort`**。**`sslConns_`** 用互斥锁保护；**`SslConnection` 初始化失败不入表并关连**。

### 多线程与 `setThreadNum`

启用 **多 IO 线程** 时，框架侧已对 **`Router`、中间件链、会话存储、`sslConns_`** 等共享可变状态加锁或读写锁。业务若在**独立线程**中读写会话、全局单例等，仍需自行保证与请求线程的同步（锁或投递回 IO 线程）。

---

## 说明

- **请求体、Content-Length 解析、413、请求头大小写**：见上文「HTTP 解析约定」。
- **响应 Content-Length、日志与 500 文案**：见上文「响应与错误处理（框架）」。
- **动态路由分桶与并发锁**：见上文「模块说明」及「多线程与 setThreadNum」。
- **请求体与 chunked**：仅 **`Content-Length`**，无 **`chunked`**（与 pipelining 说明同上节）。
- **HTTP/1.1 pipelining**：本仓库默认不实现。
- **HTTPS / TLS 细节**：见上文 **「HTTPS / TLS（SslConnection 实现要点）」**；**`setSslConfig`** 与 **`sslConns_`** 见 **「模块说明」**。
- **`HttpServer/src/session/`** 等源码中的头文件引用建议使用 **`../../include/...`** 相对路径，便于 IDE/clangd 在无完整 `compile_commands` 时也能解析。
- **`SslConfig::verifyClient_` 等**：**未实现 mTLS**；见上文 **「HTTPS / TLS」** 末节。
