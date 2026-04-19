#pragma once

#include <muduo/net/TcpServer.h>

namespace http
{

class HttpResponse 
{
public:
    enum HttpStatusCode
    {
        kUnknown,                    // 未知状态（初始值、错误或未设置时使用）
        k200Ok = 200,                // 200 OK，请求成功
        k204NoContent = 204,         // 204 无内容，服务器成功处理请求但没有返回内容
        k301MovedPermanently = 301,  // 301 永久重定向，请求的资源已被永久移动到新位置
        k400BadRequest = 400,        // 400 错误请求，客户端请求存在语法错误
        k401Unauthorized = 401,      // 401 未授权，请求要求用户认证
        k403Forbidden = 403,         // 403 禁止访问，服务器理解请求但拒绝执行
        k404NotFound = 404,          // 404 未找到，请求的资源不存在
        k409Conflict = 409,          // 409 冲突，请求与服务器的当前状态冲突
        k500InternalServerError = 500// 500 服务器内部错误，服务器遇到未曾预料的状况
    };

    HttpResponse(bool close = true)
        : statusCode_(kUnknown)
        , closeConnection_(close)
    {}

    void setVersion(std::string version)
    { httpVersion_ = version; }
    void setStatusCode(HttpStatusCode code)
    { statusCode_ = code; }

    HttpStatusCode getStatusCode() const
    { return statusCode_; }

    void setStatusMessage(const std::string message)
    { statusMessage_ = message; }

    void setCloseConnection(bool on)
    { closeConnection_ = on; }

    bool closeConnection() const
    { return closeConnection_; }
    
    void setContentType(const std::string& contentType)
    { addHeader("Content-Type", contentType); }

    void setContentLength(uint64_t length)
    { addHeader("Content-Length", std::to_string(length)); }

    void addHeader(const std::string& key, const std::string& value)
    { headers_[key] = value; }
    
    void setBody(const std::string& body)
    { 
        body_ = body;
        // body_ += "\0";
    }

    void setStatusLine(const std::string& version,
                         HttpStatusCode statusCode,
                         const std::string& statusMessage);

    void setErrorHeader(){}

    void appendToBuffer(muduo::net::Buffer* outputBuf) const;
private:
    std::string                        httpVersion_; 
    HttpStatusCode                     statusCode_;
    std::string                        statusMessage_;
    bool                               closeConnection_;
    std::map<std::string, std::string> headers_;
    std::string                        body_;
    bool                               isFile_;
};

} // namespace httpc