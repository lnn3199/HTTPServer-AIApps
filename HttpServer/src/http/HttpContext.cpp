#include "../../include/http/HttpContext.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
using namespace muduo;
using namespace muduo::net;

namespace http
{

// 将报文解析出来将关键信息封装到HttpRequest对象里面去
bool HttpContext::parseRequest(Buffer *buf, Timestamp receiveTime)
{
    parseErrorStatus_ = 400;
    bool ok = true; // 解析每行请求格式是否正确
    bool hasMore = true;
    while (hasMore)
    {
        if (state_ == kExpectRequestLine)
        {
            // 这一行用于查找缓冲区中"\r\n"的指针位置，也就是一行的结束分隔符
            // findCRLF()是Buffer类中的一个方法，用于在buf内查找下一个回车换行对（"\r\n"），返回其指针位置
            const char *crlf = buf->findCRLF(); 
            if (crlf)
            {
                // peek() 是 Buffer 类中的方法，返回当前缓冲区可读数据的起始指针
                ok = processRequestLine(buf->peek(), crlf);
                if (ok)
                {
                    request_.setReceiveTime(receiveTime);
                    // 这是 Buffer 类中的 retrieveUntil 方法，用于回收（丢弃）从缓冲区起始位置到指定 crlf+2 指针（即包括"\r\n"）之间的所有数据
                    buf->retrieveUntil(crlf + 2);
                    state_ = kExpectHeaders;
                }
                else
                {
                    hasMore = false;
                }
            }
            else
            {
                hasMore = false;
            }
        }
        else if (state_ == kExpectHeaders)
        {
            const char *crlf = buf->findCRLF();
            if (crlf)
            {
                const char *colon = std::find(buf->peek(), crlf, ':');
                if (colon < crlf)
                {
                    request_.addHeader(buf->peek(), colon, crlf);
                }
                else if (buf->peek() == crlf)//每次 onMessage 都可能往 buf 里追加数据，然后再次调用 parseRequest。
                { 
                    // 空行，结束Header
                    // 根据请求方法和Content-Length判断是否需要继续读取body
                    if (request_.method() == HttpRequest::kPost || 
                        request_.method() == HttpRequest::kPut)
                    {
                        const std::string &contentLength =
                            request_.getHeader("Content-Length");
                        if (!contentLength.empty())
                        {
                            std::uint64_t len = 0;
                            const char *first = contentLength.data();
                            const char *last = first + contentLength.size();
                            auto r = std::from_chars(first, last, len);
                            if (r.ec != std::errc{} || r.ptr != last)
                            {
                                ok = false;
                                hasMore = false;
                            }
                            else if (len > maxBodyBytes_)
                            {
                                parseErrorStatus_ = 413;
                                ok = false;
                                hasMore = false;
                            }
                            else
                            {
                                request_.setContentLength(len);
                                if (request_.contentLength() > 0)
                                {
                                    state_ = kExpectBody;
                                }
                                else
                                {
                                    state_ = kGotAll;
                                    hasMore = false;
                                }
                            }
                        }
                        else
                        {
                            // POST/PUT 请求没有 Content-Length，是HTTP语法错误
                            ok = false;
                            hasMore = false;
                        }
                    }
                    else
                    {
                        // GET/HEAD/DELETE 等方法直接完成（没有请求体）
                        state_ = kGotAll; 
                        hasMore = false;
                    }
                }
                else
                {
                    ok = false; // Header行格式错误
                    hasMore = false;
                }
                buf->retrieveUntil(crlf + 2); // 开始读指针指向下一行数据
            }
            else
            {
                hasMore = false;
            }
        }
        else if (state_ == kExpectBody)
        {
            // 检查缓冲区中是否有足够的数据
            if (buf->readableBytes() < request_.contentLength())
            {
                hasMore = false; // 数据不完整，等待更多数据
                return true;
            }

            // 只读取 Content-Length 指定的长度
            std::string body(buf->peek(), buf->peek() + request_.contentLength());
            request_.setBody(body);

            // 准确移动读指针
            buf->retrieve(request_.contentLength());

            state_ = kGotAll;
            hasMore = false;
        }
    }
    return ok; // ok为false代表报文语法解析错误
}

// 解析请求行
bool HttpContext::processRequestLine(const char *begin, const char *end)
{
    bool succeed = false;
    const char *start = begin;
    const char *space = std::find(start, end, ' ');
    if (space != end && request_.setMethod(start, space))
    {
        start = space + 1;
        space = std::find(start, end, ' ');
        if (space != end)
        {
            // std::find 返回的是指针范围内(迭代器范围内)第一个找到的 '?' 的位置，如果未找到，返回 space，和 std::string::npos 不同。
            const char *argumentStart = std::find(start, space, '?');
            if (argumentStart != space) // 请求带参数
            {
                request_.setPath(start, argumentStart); // 注意这些返回值边界
                request_.setQueryParameters(argumentStart + 1, space);
            }
            else // 请求不带参数
            {
                request_.setPath(start, space);
            }

            start = space + 1;
            // 下面代码用于判断请求行的 HTTP 版本格式是否为 "HTTP/1.x"
            // 其中(end - start == 8)表示剩余长度应为8个字符（例如 "HTTP/1.1"）
            // std::equal 用来比较 start 到 end - 1 之间的内容与 "HTTP/1." 是否一致
            succeed = ((end - start == 8) && std::equal(start, end - 1, "HTTP/1."));
            if (succeed)
            {
                if (*(end - 1) == '1')
                {
                    request_.setVersion("HTTP/1.1");
                }
                else if (*(end - 1) == '0')
                {
                    request_.setVersion("HTTP/1.0");
                }
                else
                {
                    succeed = false;
                }
            }
        }
    }
    return succeed;
}

} // namespace http