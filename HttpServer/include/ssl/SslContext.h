#pragma once
#include "SslConfig.h"
#include <openssl/ssl.h>
#include <muduo/base/noncopyable.h>

namespace ssl
{

class SslContext : muduo::noncopyable
{
public:
    explicit SslContext(const SslConfig& config);
    ~SslContext();

    bool initialize();
    SSL_CTX* getNativeHandle() { return ctx_; }//获取原始SSL_CTX指针

private:
    bool loadCertificates();//加载证书和私钥
    bool setupProtocol();//设置协议版本和相关参数
    void setupSessionCache();//配置 session cache 参数
    static void handleSslError(const char* msg);

private:
    SSL_CTX*  ctx_;//服务端 TLS 上下文
    SslConfig config_;
};

} // namespace ssl
