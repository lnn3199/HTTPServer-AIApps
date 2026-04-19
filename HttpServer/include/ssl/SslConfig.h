#pragma once
#include "SslTypes.h"
#include <string>

namespace ssl
{

class SslConfig
{
public:
    SslConfig();
    ~SslConfig() = default;

    void setCertificateFile(const std::string& certFile) { certFile_ = certFile; }
    void setPrivateKeyFile(const std::string& keyFile) { keyFile_ = keyFile; }
    void setCertificateChainFile(const std::string& chainFile) { chainFile_ = chainFile; }

    void setProtocolVersion(SSLVersion version) { version_ = version; }
    void setCipherList(const std::string& cipherList) { cipherList_ = cipherList; }

    void setVerifyClient(bool verify) { verifyClient_ = verify; }
    void setVerifyDepth(int depth) { verifyDepth_ = depth; }

    void setSessionTimeout(int seconds) { sessionTimeout_ = seconds; }
    void setSessionCacheSize(long size) { sessionCacheSize_ = size; }

    const std::string& getCertificateFile() const { return certFile_; }
    const std::string& getPrivateKeyFile() const { return keyFile_; }
    const std::string& getCertificateChainFile() const { return chainFile_; }
    SSLVersion getProtocolVersion() const { return version_; }
    const std::string& getCipherList() const { return cipherList_; }
    bool getVerifyClient() const { return verifyClient_; }
    int getVerifyDepth() const { return verifyDepth_; }
    int getSessionTimeout() const { return sessionTimeout_; }
    long getSessionCacheSize() const { return sessionCacheSize_; }

private:
    std::string certFile_;//服务端证书
    std::string keyFile_;//服务端私钥
    std::string chainFile_;//证书链
    SSLVersion  version_;//协议版本
    std::string cipherList_;//密码列表
    bool        verifyClient_;//是否验证客户端
    int         verifyDepth_;//验证深度
    int         sessionTimeout_;//会话超时时间
    long        sessionCacheSize_;//会话缓存大小
};

} // namespace ssl
