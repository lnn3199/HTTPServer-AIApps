#include "../../include/http/HttpResponse.h"

#include <cctype>
#include <cstring>

namespace http {
namespace {
bool headerKeyEqualsIgnoreCase(const std::string &key, const char *asciiLower) {
  if (key.size() != std::strlen(asciiLower)) {
    return false;
  }
  for (std::size_t i = 0; i < key.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(key[i])) !=
        static_cast<unsigned char>(asciiLower[i])) {
      return false;
    }
  }
  return true;
}
} // namespace

void HttpResponse::appendToBuffer(muduo::net::Buffer *outputBuf) const {
  // HttpResponse封装的信息格式化输出
  char buf[32];
  // 为什么不把状态信息放入格式化字符串中，因为状态信息有长有短，不方便定义一个固定大小的内存存储
  // snprintf用于格式化HTTP响应的起始行（即 "HTTP/1.1 200 "），
  //      - 第1个参数 buf：用于存放格式化后的字符串，类型为char数组
  //      - 第2个参数 sizeof buf：指定buf的大小，防止溢出
  //      - 第3个参数 "%s %d "：格式字符串，%s表示HTTP版本字符串，%d为状态码数字
  //      - 第4个参数
  //      httpVersion_.c_str()：HTTP版本（如"HTTP/1.1"），是std::string转为C字符串!!snprintf
  //      是C语言的标准函数
  //      - 第5个参数
  //      statusCode_：HTTP状态码（如200、404等），枚举类型，数值输出
  snprintf(buf, sizeof buf, "%s %d ", httpVersion_.c_str(),
           statusCode_); // sizeof 是C++的关键字（运算符），而不是函数，所以：
                         // 对类型使用时需要括号.对变量/对象使用时可以省略括号

  outputBuf->append(buf);
  outputBuf->append(statusMessage_);
  outputBuf->append("\r\n");

  if (closeConnection_) // 思考一下这些地方是不是可以直接移入近headers_中
  {
    outputBuf->append("Connection: close\r\n");
  } else {
    outputBuf->append("Connection: Keep-Alive\r\n");
  }

  snprintf(buf, sizeof buf, "Content-Length: %zu\r\n", body_.size());
  outputBuf->append(buf);

  for (const auto &header : headers_) // 按 key 排序，因此头字段顺序是字典序
  {
    if (headerKeyEqualsIgnoreCase(header.first, "content-length")) {
      continue;
    }
    outputBuf->append(header.first);
    outputBuf->append(": ");
    outputBuf->append(header.second);
    outputBuf->append("\r\n");
  }
  outputBuf->append("\r\n");

  outputBuf->append(body_);
}

void HttpResponse::setStatusLine(const std::string &version,
                                 HttpStatusCode statusCode,
                                 const std::string &statusMessage) {
  httpVersion_ = version;
  statusCode_ = statusCode;
  statusMessage_ = statusMessage;
}

} // namespace http