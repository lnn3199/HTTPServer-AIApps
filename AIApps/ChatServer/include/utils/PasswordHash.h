#pragma once

#include <string>

namespace chat {

// PBKDF2-HMAC-SHA256 (high iteration count). Stored format:
//   pbkdf2_sha256$<iterations>$<salt_hex>$<derived_key_hex>
std::string hashPassword(const std::string& password);

// Verifies against pbkdf2_sha256$... or legacy sha256$... (auto-upgrade on success).
// If legacy sha256 matches, writes new hash to upgraded_out for DB UPDATE (may be empty otherwise).
bool verifyPassword(const std::string& plain, const std::string& stored,
                    std::string* upgraded_out = nullptr);

}  // namespace chat
