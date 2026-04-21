#include "../../include/utils/PasswordHash.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

constexpr int kPbkdf2Iterations = 310000;
constexpr size_t kSaltBytes = 16;
constexpr size_t kDerivedKeyBytes = 32;

std::string toHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool hexToBytes(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hexNibble(hex[i * 2]);
        const int lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

std::string sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    return toHex(digest, SHA256_DIGEST_LENGTH);
}

bool verifyLegacySha256(const std::string& plain, const std::string& stored) {
    constexpr const char* kPrefix = "sha256$";
    if (stored.rfind(kPrefix, 0) != 0) {
        return false;
    }
    const size_t first = stored.find('$');
    const size_t second = stored.find('$', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    const std::string saltHex = stored.substr(first + 1, second - first - 1);
    const std::string hashHex = stored.substr(second + 1);
    return sha256Hex(saltHex + plain) == hashHex;
}

bool verifyPbkdf2(const std::string& plain, const std::string& stored) {
    constexpr const char kPrefix[] = "pbkdf2_sha256$";
    if (stored.rfind(kPrefix, 0) != 0) {
        return false;
    }
    const size_t afterPrefix = sizeof(kPrefix) - 1;
    const size_t d1 = stored.find('$', afterPrefix);
    if (d1 == std::string::npos) {
        return false;
    }
    int iterations = 0;
    try {
        iterations = std::stoi(stored.substr(afterPrefix, d1 - afterPrefix));
    } catch (...) {
        return false;
    }
    if (iterations < 100000) {
        return false;
    }
    const size_t d2 = stored.find('$', d1 + 1);
    if (d2 == std::string::npos) {
        return false;
    }
    const std::string saltHex = stored.substr(d1 + 1, d2 - d1 - 1);
    const std::string dkHex = stored.substr(d2 + 1);
    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!hexToBytes(saltHex, salt) || !hexToBytes(dkHex, expected)) {
        return false;
    }
    if (expected.size() != kDerivedKeyBytes || salt.empty()) {
        return false;
    }
    unsigned char dk[kDerivedKeyBytes];
    if (PKCS5_PBKDF2_HMAC(plain.data(), static_cast<int>(plain.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(sizeof(dk)), dk) != 1) {
        return false;
    }
    return CRYPTO_memcmp(dk, expected.data(), sizeof(dk)) == 0;
}

}  // namespace

namespace chat {

std::string hashPassword(const std::string& password) {
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        throw std::runtime_error("Failed to generate password salt");
    }
    const std::string saltHex = toHex(salt, sizeof(salt));
    unsigned char dk[kDerivedKeyBytes];
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt,
                          static_cast<int>(sizeof(salt)), kPbkdf2Iterations, EVP_sha256(),
                          static_cast<int>(sizeof(dk)), dk) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
    const std::string dkHex = toHex(dk, sizeof(dk));
    return std::string("pbkdf2_sha256$") + std::to_string(kPbkdf2Iterations) + "$" + saltHex +
           "$" + dkHex;
}

bool verifyPassword(const std::string& plain, const std::string& stored,
                    std::string* upgraded_out) {
    if (verifyPbkdf2(plain, stored)) {
        return true;
    }
    if (verifyLegacySha256(plain, stored)) {
        if (upgraded_out) {
            *upgraded_out = hashPassword(plain);
        }
        return true;
    }
    // Reject legacy plaintext and unknown formats.
    return false;
}

}  // namespace chat
