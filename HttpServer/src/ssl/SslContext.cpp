#include "../../include/ssl/SslContext.h"
#include <muduo/base/Logging.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

namespace ssl
{
SslContext::SslContext(const SslConfig& config)
    : ctx_(nullptr)
    , config_(config)
{

}

SslContext::~SslContext()
{
    if (ctx_)
    {
        SSL_CTX_free(ctx_);
    }
}

bool SslContext::initialize()
{
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                    OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);

    const SSL_METHOD* method = TLS_server_method();
    ctx_ = SSL_CTX_new(method);
    if (!ctx_)
    {
        handleSslError("Failed to create SSL context");
        return false;
    }

    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                  SSL_OP_NO_COMPRESSION |
                  SSL_OP_CIPHER_SERVER_PREFERENCE;
    SSL_CTX_set_options(ctx_, options);

    if (!loadCertificates())
    {
        return false;
    }

    if (!setupProtocol())
    {
        return false;
    }

    setupSessionCache();

    LOG_INFO << "SSL context initialized successfully";
    return true;
}

bool SslContext::loadCertificates()
{
    if (SSL_CTX_use_certificate_file(ctx_,
     config_.getCertificateFile().c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        handleSslError("Failed to load server certificate");
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_,
        config_.getPrivateKeyFile().c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        handleSslError("Failed to load private key");
        return false;
    }

    if (!SSL_CTX_check_private_key(ctx_))
    {
        handleSslError("Private key does not match the certificate");
        return false;
    }

    if (!config_.getCertificateChainFile().empty())
    {
        if (SSL_CTX_use_certificate_chain_file(ctx_,
            config_.getCertificateChainFile().c_str()) <= 0)
        {
            handleSslError("Failed to load certificate chain");
            return false;
        }
    }

    return true;
}

bool SslContext::setupProtocol()
{
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    int minVer = TLS1_2_VERSION;
    int maxVer = TLS1_2_VERSION;
    switch (config_.getProtocolVersion())
    {
        case SSLVersion::TLS_1_0:
            minVer = maxVer = TLS1_VERSION;
            break;
        case SSLVersion::TLS_1_1:
            minVer = maxVer = TLS1_1_VERSION;
            break;
        case SSLVersion::TLS_1_2:
            minVer = maxVer = TLS1_2_VERSION;
            break;
        case SSLVersion::TLS_1_3:
#if defined(TLS1_3_VERSION)
            minVer = maxVer = TLS1_3_VERSION;
#else
            handleSslError("TLS 1.3 requires OpenSSL 1.1.1 or newer");
            return false;
#endif
            break;
    }
    if (SSL_CTX_set_min_proto_version(ctx_, minVer) != 1 ||
        SSL_CTX_set_max_proto_version(ctx_, maxVer) != 1)
    {
        handleSslError("Failed to set TLS protocol version");
        return false;
    }
#else
    long extra = 0;
    switch (config_.getProtocolVersion())
    {
        case SSLVersion::TLS_1_0:
            extra = SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2;
#ifdef SSL_OP_NO_TLSv1_3
            extra |= SSL_OP_NO_TLSv1_3;
#endif
            break;
        case SSLVersion::TLS_1_1:
            extra = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_2;
#ifdef SSL_OP_NO_TLSv1_3
            extra |= SSL_OP_NO_TLSv1_3;
#endif
            break;
        case SSLVersion::TLS_1_2:
            extra = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
#ifdef SSL_OP_NO_TLSv1_3
            extra |= SSL_OP_NO_TLSv1_3;
#endif
            break;
        case SSLVersion::TLS_1_3:
            handleSslError("TLS 1.3 requires OpenSSL 1.1.0 or newer");
            return false;
    }
    SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | extra);
#endif

    if (!config_.getCipherList().empty())
    {
        if (SSL_CTX_set_cipher_list(ctx_,
            config_.getCipherList().c_str()) <= 0)
        {
            handleSslError("Failed to set cipher list");
            return false;
        }
    }

    return true;
}

void SslContext::setupSessionCache()
{
    SSL_CTX_set_session_cache_mode(ctx_, SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_cache_size(ctx_, config_.getSessionCacheSize());
    SSL_CTX_set_timeout(ctx_, config_.getSessionTimeout());
}

void SslContext::handleSslError(const char* msg)
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    LOG_ERROR << msg << ": " << buf;
}

} // namespace ssl
