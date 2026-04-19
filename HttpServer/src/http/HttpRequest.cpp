#include "../../include/http/HttpRequest.h"

namespace http
{

void HttpRequest::setReceiveTime(muduo::Timestamp t)
{
    receiveTime_ = t;
}

bool HttpRequest::setMethod(const char *start, const char *end)
{
    // 这行代码的意思是：如果method_不是kInvalid（说明已经被赋值过），程序会中断并抛出断言错误。
    // 用途是防止重复设置HTTP请求方法，确保setMethod只在method_未初始化时才被调用，一旦被设置就不允许再次更改。
    assert(method_ == kInvalid);
    std::string m(start, end); // [start, end)
    if (m == "GET")
    {
        method_ = kGet;
    }
    else if (m == "POST")
    {
        method_ = kPost;
    }
    else if (m == "PUT")
    {
        method_ = kPut;
    }
    else if (m == "DELETE")
    {
        method_ = kDelete;
    }
    else if (m == "OPTIONS")
    {
        method_ = kOptions;
    }
    else
    {
        method_ = kInvalid;
    }

    return method_ != kInvalid;
}

void HttpRequest::setPath(const char *start, const char *end)
{
    // 这是C++中std::string的assign方法，用于将区间[start, end)的字符赋值给path_
    path_.assign(start, end); // 从请求行提取URL路径，不包括查询参数(query)
}

void HttpRequest::setPathParameters(const std::string &key, const std::string &value)
{
    pathParameters_[key] = value;
}

std::string HttpRequest::getPathParameters(const std::string &key) const
{
    auto it = pathParameters_.find(key);
    if (it != pathParameters_.end())
    {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getQueryParameters(const std::string &key) const
{
    auto it = queryParameters_.find(key);
    if (it != queryParameters_.end())
    {
        return it->second;
    }
    return "";
}

// 这是从问号后面分割参数
void HttpRequest::setQueryParameters(const char *start, const char *end)
{
    // 把[start, end)区间的内容作为一个新的字符串（即参数字符串，如 "id=10&name=abc"）
    std::string argumentStr(start, end);

    // pos用于查找'&'的位置，prev用来标记当前参数起始位置
    std::string::size_type pos = 0;
    std::string::size_type prev = 0;

    // 通过循环，依次处理每一个"key=value"对，中间由'&'分隔
    while ((pos = argumentStr.find('&', prev)) != std::string::npos)
    {
        // 取出当前的一个参数对，从prev开始到pos前一位，即[prev, pos)
        // 是左闭右开：substr的参数含义是，从prev开始（包括prev），取pos-prev个字符，到pos截止但不包括pos
        // argumentStr.substr(prev, pos - prev) 返回的是 [prev, pos) 区间的内容
        std::string pair = argumentStr.substr(prev, pos - prev);
        // 查找参数对中'='的分隔位置
        std::string::size_type equalPos = pair.find('=');

        // 如果存在'='，说明参数有效
        if (equalPos != std::string::npos)
        {
            // key为'='左边，value为右边
            std::string key = pair.substr(0, equalPos);
            std::string value = pair.substr(equalPos + 1);
            // 保存到queryParameters_成员变量
            queryParameters_[key] = value;
        }

        // 更新prev，处理下一个参数对
        prev = pos + 1;
    }

    // 处理最后一个参数（因为最后一个参数后面没'&'，循环里不会处理）
    std::string lastPair = argumentStr.substr(prev);
    std::string::size_type equalPos = lastPair.find('=');
    if (equalPos != std::string::npos)
    {
        std::string key = lastPair.substr(0, equalPos);
        std::string value = lastPair.substr(equalPos + 1);
        queryParameters_[key] = value;
    }
}

void HttpRequest::addHeader(const char *start, const char *colon, const char *end)
{
    std::string key(start, colon);
    ++colon;

    // 3. 跳过冒号后所有的空白符：while + isspace
    //    - colon < end：防止越界。
    //    - isspace(*colon)：判断是否为空格/制表符等空白字符。
    //    - 特殊语法：*colon 取指针指向的内容
    while (colon < end && isspace(*colon))
    {
        ++colon;
    }
    std::string value(colon, end);

    // 5. 去掉value尾部的所有空白符。
    //    - value.empty() 防止越界。
    //    - isspace(value[value.size()-1]) 检查最后一个字符是否为空白。
    //    - value.resize(...) 截断末尾空格
    while (!value.empty() && isspace(value[value.size() - 1])) // 消除尾部空格
    {
        value.resize(value.size() - 1);
    }
    headers_[key] = value;
}

std::string HttpRequest::getHeader(const std::string &field) const
{
    std::string result;
    auto it = headers_.find(field);
    if (it != headers_.end())
    {
        result = it->second;
    }
    return result;
}

void HttpRequest::swap(HttpRequest &that)//swap 没有交换 body/contentLength
{
    std::swap(method_, that.method_);
    std::swap(path_, that.path_);
    std::swap(pathParameters_, that.pathParameters_);
    std::swap(queryParameters_, that.queryParameters_);
    std::swap(version_, that.version_);
    std::swap(headers_, that.headers_);
    std::swap(receiveTime_, that.receiveTime_);
}

} // namespace http