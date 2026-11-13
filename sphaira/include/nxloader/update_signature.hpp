#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace nxloader::self_update {

using Sha256 = std::array<std::uint8_t, 32>;

// Checks an ECDSA P-256 signature over a SHA-256 digest. The signature is DER,
// as written by `openssl dgst -sha256 -sign`. public_key is a DER
// SubjectPublicKeyInfo and must be a P-256 key.
bool VerifySignature(std::span<const std::uint8_t> public_key, const Sha256& digest,
    std::span<const std::uint8_t> signature);

} // namespace nxloader::self_update
