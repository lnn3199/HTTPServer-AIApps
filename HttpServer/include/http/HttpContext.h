#pragma once

#include <cstdint>
#include <iostream>

#include <muduo/net/TcpServer.h>

#include "HttpRequest.h"

namespace http
{

class HttpContext 
{
public:
    enum HttpRequestParseState
    {
        kExpectRequestLine, // 解析请求行
        kExpectHeaders, // 解析请求头
        kExpectBody, // 解析请求体
        kGotAll, // 解析完成
    };
    
    explicit HttpContext(std::uint64_t maxBodyBytes = 8ULL * 1024 * 1024)
    : state_(kExpectRequestLine), maxBodyBytes_(maxBodyBytes) {}

    bool parseRequest(muduo::net::Buffer* buf, muduo::Timestamp receiveTime);
    bool gotAll() const 
    { return state_ == kGotAll;  }

    int parseErrorStatus() const { return parseErrorStatus_; }

    void reset()
    {
        state_ = kExpectRequestLine;
        parseErrorStatus_ = 400;
        HttpRequest dummyData;
        request_.swap(dummyData);
    }

    const HttpRequest& request() const
    { return request_;}

    HttpRequest& request()
    { return request_;}

private:
    bool processRequestLine(const char* begin, const char* end);
private:
    HttpRequestParseState state_;
    HttpRequest           request_;
    std::uint64_t maxBodyBytes_;
    int           parseErrorStatus_{400};
};

} // namespace http