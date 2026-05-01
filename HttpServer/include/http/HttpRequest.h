#pragma once

#include <map>
#include <string>
// 两者区别：unordered_map 底层是哈希表，查找/插入效率高但无序；map 底层是红黑树，存储有序但效率略低。
#include <unordered_map>

#include <muduo/base/Timestamp.h>

namespace http
{

class HttpRequest
{
public:
    // 使用 enum 类型定义 HTTP 方法的原因:
    // 1. 可读性和类型安全：枚举类型明确限定了 HttpRequest::Method 只能是预设的一组 HTTP 方法，减少了因字符串比较、硬编码产生的错误。
    // 2. 编译器检查：用 enum 而不是字符串，可以让编译器帮助发现变量赋值错误、遗漏 case 等问题，提高代码安全性和健壮性。
    // 3. 性能：使用枚举常量在比较和分支判断时效率高于字符串（整数比较比字符串快很多）。
    enum Method
    {
        kInvalid, // 无效方法，初始化默认值
        kGet,     // GET方法：用于获取资源（最常用）
        kPost,    // POST方法：用于提交数据（如表单等）
        kHead,    // HEAD方法：仅请求获取资源的响应首部，不返回实际内容
        kPut,     // PUT方法：用于上传整个资源（覆盖）
        kDelete,  // DELETE方法：请求删除服务器资源
        kOptions  // OPTIONS方法：用于请求服务器支持的HTTP方法以及相关选项
    };
    
    HttpRequest()
        : method_(kInvalid)
        , version_("Unknown")
    {
    }
    
    void setReceiveTime(muduo::Timestamp t);
    muduo::Timestamp receiveTime() const { return receiveTime_; }
    
    bool setMethod(const char* start, const char* end);
    Method method() const { return method_; }

    void setPath(const char* start, const char* end);
    std::string path() const { return path_; }

    void setPathParameters(const std::string &key, const std::string &value);
    std::string getPathParameters(const std::string &key) const;

    void setQueryParameters(const char* start, const char* end);
    std::string getQueryParameters(const std::string &key) const;
    
    void setVersion(std::string v)
    {
        version_ = v;
    }

    std::string getVersion() const
    {
        return version_;
    }
    
    void addHeader(const char* start, const char* colon, const char* end);
    /** 字段名不区分大小写；内部以小写存储，查找时也会将 field 转为小写。 */
    std::string getHeader(const std::string& field) const;

    /** 请求头字段名均为小写 key（HTTP 语义中场名不区分大小写）。 */
    const std::map<std::string, std::string>& headers() const
    { return headers_; }

    void setBody(const std::string& body) { content_ = body; }
    void setBody(const char* start, const char* end) 
    { 
        // 这里的 end 通常指向应该复制的区间的“下一个地址”，即 [start, end) 左闭右开区间。
      
        if (end >= start) 
        {
            //   - 从 start 位置开始，取 (end - start) 个字符（不包括 end 指向的字符），赋值给 content_ 字符串成员。
            content_.assign(start, end - start); 
        }
    }
    
    std::string getBody() const
    { return content_; }

    void setContentLength(uint64_t length)
    { contentLength_ = length; }
    
    uint64_t contentLength() const
    { return contentLength_; }

    void swap(HttpRequest& that);

private:
    Method                                       method_; // 请求方法
    std::string                                  version_; // http版本
    std::string                                  path_; // 请求路径
    std::unordered_map<std::string, std::string> pathParameters_; // 路径参数
    std::unordered_map<std::string, std::string> queryParameters_; // 查询参数
    muduo::Timestamp                             receiveTime_; // 接收时间
    std::map<std::string, std::string>           headers_; // 请求头
    std::string                                  content_; // 请求体
    uint64_t                                     contentLength_ { 0 }; // 请求体长度
};  

} // namespace http