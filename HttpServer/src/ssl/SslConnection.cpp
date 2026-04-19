#include "../../include/ssl/SslConnection.h"
#include <algorithm>
#include <muduo/base/Logging.h>
#include <openssl/err.h>

namespace ssl {

SslConnection::SslConnection(const TcpConnectionPtr &conn, SslContext *ctx)
    : ssl_(nullptr), ctx_(ctx), conn_(conn), state_(SSLState::HANDSHAKE),
      readBio_(nullptr), writeBio_(nullptr) {
  ssl_ = SSL_new(ctx_->getNativeHandle());
  if (!ssl_) {
    LOG_ERROR << "Failed to create SSL object: "
              << ERR_error_string(ERR_get_error(), nullptr);
    return;
  }

  readBio_ = BIO_new(BIO_s_mem());
  writeBio_ = BIO_new(BIO_s_mem());

  if (!readBio_ || !writeBio_) {
    LOG_ERROR << "Failed to create BIO objects";
    SSL_free(ssl_);
    ssl_ = nullptr;
    return;
  }

  SSL_set_bio(ssl_, readBio_, writeBio_);
  SSL_set_accept_state(ssl_);

  SSL_set_mode(ssl_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);
  // 不覆盖 TcpConnection 的 messageCallback：由 HttpServer::onMessage 统一入口，
  // 内部再调用 onRead，解密后用 getDecryptedBuffer() 做 HTTP 解析。
}

SslConnection::~SslConnection() {
  if (ssl_) {
    SSL_free(ssl_);
  }
}

void SslConnection::flushWriteBio() {
  char buf[4096];
  int pending = 0;
  while (ssl_ && writeBio_ && (pending = BIO_pending(writeBio_)) > 0) {
    int bytes = BIO_read(writeBio_, buf,
                         std::min(pending, static_cast<int>(sizeof(buf))));
    if (bytes > 0) {
      conn_->send(buf, bytes);
    } else {
      break;
    }
  }
}

void SslConnection::startHandshake() {
  if (!ssl_) {
    LOG_ERROR << "Cannot start handshake: ssl_ is null";
    state_ = SSLState::ERROR;
    return;
  }
  SSL_set_accept_state(ssl_);
  handleHandshake();
}

void SslConnection::send(const void *data, size_t len) {
  if (!ssl_) {
    LOG_ERROR << "Cannot send: ssl_ is null";
    return;
  }
  if (state_ != SSLState::ESTABLISHED) {
    LOG_ERROR << "Cannot send data before SSL handshake is complete";
    return;
  }

  int written = SSL_write(ssl_, data, len);
  if (written <= 0) {
    int err = SSL_get_error(ssl_, written);
    LOG_ERROR << "SSL_write failed, SSL_get_error=" << err;
    flushWriteBio();
    return;
  }

  flushWriteBio();
}

void SslConnection::onRead(const TcpConnectionPtr &conn, BufferPtr buf,
                           muduo::Timestamp time) {
  // 标记参数未使用，避免编译器警告
  (void)conn;
  (void)time;

  // 1. 检查SSL对象是否有效
  if (!ssl_) {
    LOG_ERROR << "Cannot read: ssl_ is null";
    state_ = SSLState::ERROR;
    return;
  }

  // 2. 如果还在握手阶段，处理握手数据
  if (state_ == SSLState::HANDSHAKE) {
    // 将TCP缓冲区的数据写入SSL的readBio(BIO输入缓存)
    BIO_write(readBio_, buf->peek(), buf->readableBytes());
    // 移动缓冲区读取位置，相当于消费掉这些数据
    buf->retrieve(buf->readableBytes());
    // 继续尝试SSL握手
    handleHandshake();
    return;
  }
  // 3. 如果SSL已经建立，则从BIO读取TLS数据并解密
  else if (state_ == SSLState::ESTABLISHED) {
    // 检查是否还有数据可读
    if (buf->readableBytes() == 0) {
      return;
    }
    // 把从TCP收到的加密数据写入readBio_
    BIO_write(readBio_, buf->peek(), buf->readableBytes());
    // 消费缓冲区内的数据
    buf->retrieve(buf->readableBytes());

    char decryptedData[4096];
    int ret = 0;
    // 尝试解密
    while ((ret = SSL_read(ssl_, decryptedData, sizeof(decryptedData))) > 0) {
      // 每次成功解密出的数据都追加到decryptedBuffer_中
      decryptedBuffer_.append(decryptedData, ret);
    }
    // 如果解密读取失败
    if (ret < 0) {
      SSLError error = getLastError(ret);
      // 遇到非预期的错误就处理之
      if (error != SSLError::WANT_READ && error != SSLError::WANT_WRITE) {
        handleError(error);
      }
    }
    // 把SSL write BIO 里可能缓存的加密数据发给对端
    flushWriteBio();
  }
}

void SslConnection::handleHandshake() {
  int ret = SSL_do_handshake(ssl_);

  if (ret == 1) {
    state_ = SSLState::ESTABLISHED;
    LOG_INFO << "SSL handshake completed successfully";
    LOG_INFO << "Using cipher: " << SSL_get_cipher(ssl_);
    LOG_INFO << "Protocol version: " << SSL_get_version(ssl_);
    flushWriteBio();
    return;
  }

  int err = SSL_get_error(ssl_, ret);
  switch (err) {
  case SSL_ERROR_WANT_READ:
  case SSL_ERROR_WANT_WRITE:
    break;

  default: {
    char errBuf[256];
    unsigned long errCode = ERR_get_error();
    ERR_error_string_n(errCode, errBuf, sizeof(errBuf));
    LOG_ERROR << "SSL handshake failed: " << errBuf;
    conn_->shutdown();
    break;
  }
  }
  flushWriteBio();
}

void SslConnection::onEncrypted(const char *data, size_t len) {
  writeBuffer_.append(data, len);
  conn_->send(&writeBuffer_);
}

void SslConnection::onDecrypted(const char *data, size_t len) {
  decryptedBuffer_.append(data, len);
}

SSLError SslConnection::getLastError(int ret) {
  int err = SSL_get_error(ssl_, ret);
  switch (err) {
  case SSL_ERROR_NONE:
    return SSLError::NONE;
  case SSL_ERROR_WANT_READ:
    return SSLError::WANT_READ;
  case SSL_ERROR_WANT_WRITE:
    return SSLError::WANT_WRITE;
  case SSL_ERROR_SYSCALL:
    return SSLError::SYSCALL;
  case SSL_ERROR_SSL:
    return SSLError::SSL;
  default:
    return SSLError::UNKNOWN;
  }
}

void SslConnection::handleError(SSLError error) {
  switch (error) {
  case SSLError::WANT_READ:
  case SSLError::WANT_WRITE:
    break;
  case SSLError::SSL:
  case SSLError::SYSCALL:
  case SSLError::UNKNOWN:
    LOG_ERROR << "SSL error occurred: "
              << ERR_error_string(ERR_get_error(), nullptr);
    state_ = SSLState::ERROR;
    conn_->shutdown();
    break;
  default:
    break;
  }
}

int SslConnection::bioWrite(BIO *bio, const char *data, int len) {
  SslConnection *c = static_cast<SslConnection *>(BIO_get_data(bio));
  if (!c)
    return -1;

  c->conn_->send(data, len);
  return len;
}

int SslConnection::bioRead(BIO *bio, char *data, int len) {
  SslConnection *c = static_cast<SslConnection *>(BIO_get_data(bio));
  if (!c)
    return -1;

  size_t readable = c->readBuffer_.readableBytes();
  if (readable == 0) {
    return -1;
  }

  size_t toRead = std::min(static_cast<size_t>(len), readable);
  memcpy(data, c->readBuffer_.peek(), toRead);
  c->readBuffer_.retrieve(toRead);
  return static_cast<int>(toRead);
}

long SslConnection::bioCtrl(BIO *bio, int cmd, long num, void *ptr) {
  switch (cmd) {
  case BIO_CTRL_FLUSH:
    return 1;
  default:
    return 0;
  }
}

} // namespace ssl
