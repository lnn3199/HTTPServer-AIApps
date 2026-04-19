#include "../../include/http/HttpServer.h"
#include "../../include/http/HttpContext.h"

#include <functional> //std::function 是 C++11 引入的标准库头文件，提供类型安全的函数包装器 std::function。如需使用 std::function，需包含：#include <functional>
#include <memory>

namespace http {

namespace {
constexpr char kHttp400[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
}

void HttpServer::sendToClient(const muduo::net::TcpConnectionPtr &conn,
                              const void *data, size_t len) {
  if (len == 0) {
    return;
  }
  if (useSSL_) {
    auto it = sslConns_.find(conn);
    if (it != sslConns_.end() && it->second->isHandshakeCompleted()) {
      it->second->send(data, len);
      return;
    }
    LOG_ERROR << "sendToClient: SSL enabled but connection is not ready for encrypted send";
    return;
  }
  conn->send(data, len);
}

// 默认http回应函数
void defaultHttpCallback(const HttpRequest &, HttpResponse *resp) {
  resp->setStatusCode(HttpResponse::k404NotFound);
  resp->setStatusMessage("Not Found");
  resp->setCloseConnection(true);
}

HttpServer::HttpServer(
    int port, const std::string &name, bool useSSL,
    // 这是muduo库中TcpServer的端口复用选项参数，用于控制监听端口的绑定行为（如是否允许端口复用）,允许程序在短时间内重启而不会报错。
    muduo::net::TcpServer::Option option)
    : listenAddr_(port)
      // 初始化TcpServer对象，参数依次为：
      // 1. &mainLoop_      :
      // 传入EventLoop主事件循环指针，用于事件驱动处理网络IO
      // 2. listenAddr_     :
      // 服务器监听的IP地址和端口（InetAddress对象，内部已由port初始化）
      // 3. name            : 服务器实例名称，用于日志等标识
      // 4. option          : 端口复用选项（kNoReusePort等），控制端口绑定行为
      ,
      server_(&mainLoop_, listenAddr_, name, option), useSSL_(useSSL),
      httpCallback_(
          [this](const http::HttpRequest &req, http::HttpResponse *resp) {
            this->handleRequest(req, resp);
          }) // bind可以隐式转换为std::function,默认会走 handleRequest 作为 HTTP
             // 处理回调
{
  initialize();
}

// 服务器运行函数
// 说明：
// - server_.ipPort() 是 muduo::TcpServer 的成员方法，它返回服务器监听的 IP:端口
// 形式的字符串，常用于日志输出服务器监听的地址端口，如 "0.0.0.0:8080"。
// - server_.start() 是启动 muduo::TcpServer
// 的方法，会让服务器开始监听套接字和接受连接（实际启动工作线程）。
// - mainLoop_.loop() 则是主 EventLoop 的事件循环，使主线程持续处理所有可用 IO
// 事件，只有调用 loop() 才会进入事件驱动模式，不会立即返回。
// 合起来，这段代码用于启动服务器、进入事件循环，实际运行 HTTP 服务器的主逻辑：
void HttpServer::start() {
  LOG_WARN << "HttpServer[" << server_.name() << "] starts listening on"
           << server_.ipPort();
  server_.start();  // 让 Muduo 开始监听并注册事件
  mainLoop_.loop(); // 阻塞进入事件循环
}

void HttpServer::initialize() {
  // 设置回调函数
  server_.setConnectionCallback(
      [this](const muduo::net::TcpConnectionPtr &conn) {
        this->onConnection(conn);
      });
  // 当连接建立/断开时触发 HttpServer::onConnection
  server_.setMessageCallback([this](const muduo::net::TcpConnectionPtr &conn,
                                    muduo::net::Buffer *buf,
                                    muduo::Timestamp receiveTime) {
    this->onMessage(conn, buf, receiveTime);
  }); // 当收到数据时触发 HttpServer::onMessage
}

void HttpServer::setSslConfig(const ssl::SslConfig &config) {
  if (useSSL_) {
    sslCtx_ = std::make_unique<ssl::SslContext>(config);
    if (!sslCtx_->initialize()) {
      LOG_ERROR << "Failed to initialize SSL context";
      abort();
    }
  }
}

void HttpServer::onConnection(const muduo::net::TcpConnectionPtr &conn) {
  if (conn->connected()) {
    if (useSSL_) {
      if (!sslCtx_) {
        LOG_ERROR << "useSSL is true but SSL context is not initialized (call setSslConfig first)";
        conn->shutdown();
        return;
      }
      auto sslConn = std::make_unique<ssl::SslConnection>(conn, sslCtx_.get());
      sslConns_[conn] = std::move(sslConn);
      sslConns_[conn]->startHandshake();
    }
    // 每条TCP连接都设置自己的HttpContext到上下文中：
    // HttpContext负责缓存和解析HTTP请求的状态
    conn->setContext(HttpContext());
  } else {
    if (useSSL_) {
      sslConns_.erase(conn);
    }
  }
}

void HttpServer::onMessage(const muduo::net::TcpConnectionPtr &conn,
                           muduo::net::Buffer *buf,
                           muduo::Timestamp receiveTime) {
  try {
    if (useSSL_) {
      auto it = sslConns_.find(conn);
      if (it == sslConns_.end()) {
        LOG_ERROR << "onMessage: useSSL but no SslConnection for this connection "
                     "(internal state error)";
        conn->shutdown();
        return;
      }
      it->second->onRead(conn, buf, receiveTime);

      if (!it->second->isHandshakeCompleted()) {
        return;
      }

      muduo::net::Buffer *decryptedBuf = it->second->getDecryptedBuffer();
      if (decryptedBuf->readableBytes() == 0) {
        return;
      }

      buf = decryptedBuf;
    }
    // HttpContext对象用于解析出buf中的请求报文，并把报文的关键信息封装到HttpRequest对象中
    // boost::any_cast 用于从 boost::any 类型中安全地提取所需类型（这里是
    // HttpContext）的指针。 conn->getMutableContext() 返回一个指向
    // TcpConnection 关联上下文的 boost::any 对象， 通过 any_cast<HttpContext>()
    // 将其提取为 HttpContext* 指针类型。
    HttpContext *context =
        boost::any_cast<HttpContext>(conn->getMutableContext());
    if (!context->parseRequest(buf, receiveTime)) // 解析一个http请求
    {
      // 如果解析http报文过程中出错
      sendToClient(conn, kHttp400, sizeof(kHttp400) - 1);
      conn->shutdown();
    }
    // 如果buf缓冲区中解析出一个完整的数据包才封装响应报文
    if (context->gotAll()) {
      onRequest(conn, context->request());
      context->reset();
    }
  } catch (const std::exception &e) {
    // 捕获异常，返回错误信息
    LOG_ERROR << "Exception in onMessage: " << e.what();
    sendToClient(conn, kHttp400, sizeof(kHttp400) - 1);
    conn->shutdown();
  }
}

void HttpServer::onRequest(const muduo::net::TcpConnectionPtr &conn,
                           const HttpRequest &req) {
  const std::string &connection = req.getHeader("Connection");
  bool close = ((connection == "close") ||
                (req.getVersion() == "HTTP/1.0" && connection != "Keep-Alive"));
  HttpResponse response(close);

  // 根据请求报文信息来封装响应报文对象
  httpCallback_(req, &response); // 执行handleRequest函数

  // 可以给response设置一个成员，判断是否请求的是文件，如果是文件设置为true，并且存在文件位置在这里send出去。
  muduo::net::Buffer buf;
  response.appendToBuffer(&buf);
  // 打印完整的响应内容用于调试

  // buf.toStringPiece().as_string()
  // 的作用是将muduo::net::Buffer内容转为std::string，便于直接输出请求响应内容。
  LOG_INFO << "Sending response:\n" << buf.toStringPiece().as_string();

  sendToClient(conn, buf.peek(), buf.readableBytes());
  // 如果是短连接的话，返回响应报文后就断开连接
  if (response.closeConnection()) {
    conn->shutdown();
  }
}

// 执行请求对应的路由处理函数
void HttpServer::handleRequest(const HttpRequest &req, HttpResponse *resp) {
  try {
    // 处理请求前的中间件
    HttpRequest mutableReq = req;
    middlewareChain_.processBefore(mutableReq);

    // 路由处理
    if (!router_.route(mutableReq, resp)) {
      LOG_INFO << "请求的啥，url：" << req.method() << " " << req.path();
      LOG_INFO << "未找到路由，返回404";
      resp->setStatusCode(HttpResponse::k404NotFound);
      resp->setStatusMessage("Not Found");
      resp->setCloseConnection(true);
    }

    // 处理响应后的中间件
    middlewareChain_.processAfter(*resp);
  } catch (const HttpResponse &res) {
    // 处理中间件抛出的响应（如CORS预检请求）
    *resp = res;
  } catch (const std::exception &e) {
    // 错误处理
    resp->setStatusCode(HttpResponse::k500InternalServerError);
    resp->setBody(e.what());
  }
}

} // namespace http