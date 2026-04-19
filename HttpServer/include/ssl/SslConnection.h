#pragma once
#include "SslContext.h"
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/noncopyable.h>
#include <openssl/ssl.h>
#include <memory>

namespace ssl
{

class SslConnection : muduo::noncopyable
{
public:
    using TcpConnectionPtr = std::shared_ptr<muduo::net::TcpConnection>;
    using BufferPtr = muduo::net::Buffer*;

    SslConnection(const TcpConnectionPtr& conn, SslContext* ctx);
    ~SslConnection();

    void startHandshake();
    void send(const void* data, size_t len);
    void onRead(const TcpConnectionPtr& conn, BufferPtr buf, muduo::Timestamp time);
    bool isHandshakeCompleted() const { return state_ == SSLState::ESTABLISHED; }
    muduo::net::Buffer* getDecryptedBuffer() { return &decryptedBuffer_; }
    static int bioWrite(BIO* bio, const char* data, int len);
    static int bioRead(BIO* bio, char* data, int len);
    static long bioCtrl(BIO* bio, int cmd, long num, void* ptr);
private:
    void flushWriteBio();
    void handleHandshake();
    void onEncrypted(const char* data, size_t len);
    void onDecrypted(const char* data, size_t len);
    SSLError getLastError(int ret);
    void handleError(SSLError error);

private:
    SSL*                ssl_;//SSL连接对象
    SslContext*         ctx_;//SSL上下文对象
    TcpConnectionPtr    conn_;//TCP连接对象
    SSLState            state_;//SSL状态
    BIO*                readBio_;//读取BIO对象
    BIO*                writeBio_;//写入BIO对象
    muduo::net::Buffer  readBuffer_;//读取缓冲区
    muduo::net::Buffer  writeBuffer_;//写入缓冲区
    muduo::net::Buffer  decryptedBuffer_;//解密缓冲区
};

} // namespace ssl
