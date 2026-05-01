#pragma once
#include "SslContext.h"
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/noncopyable.h>
#include <openssl/ssl.h>
#include <memory>
#include <vector>

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
    /** SSL/BIO 是否创建成功；失败时不应放入 sslConns_ 或调用 startHandshake 之外的 TLS 操作。 */
    bool ok() const { return ssl_ != nullptr; }
    bool isHandshakeCompleted() const { return state_ == SSLState::ESTABLISHED; }
    muduo::net::Buffer* getDecryptedBuffer() { return &decryptedBuffer_; }
    static int bioWrite(BIO* bio, const char* data, int len);
    static int bioRead(BIO* bio, char* data, int len);
    static long bioCtrl(BIO* bio, int cmd, long num, void* ptr);
private:
    void flushWriteBio();
    /** 把密文写入 readBio_；若 BIO_write 未完整接受则关连。成功返回 true。 */
    bool feedReadBio(const void *data, size_t len);
    /** 把队列里的明文尽量写入 SSL；遇 WANT_READ/WANT_WRITE 则停，等 onRead / 写完成回调再试。 */
    void drainPendingPlain();
    /** TcpConnection 输出缓冲区有空间时续写密文与挂起的明文（配合 WANT_WRITE）。 */
    void onWriteComplete();
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
    /** SSL_write 因 WANT_READ/WANT_WRITE 未写完的明文，在后续 onRead/send 中续写。 */
    std::vector<char>    pendingPlain_;
};

} // namespace ssl
