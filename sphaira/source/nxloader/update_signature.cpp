#include "nxloader/update_signature.hpp"

#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

namespace nxloader::self_update {

bool VerifySignature(std::span<const std::uint8_t> public_key, const Sha256& digest,
    std::span<const std::uint8_t> signature) {
    if (public_key.empty() || signature.empty()) {
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    bool valid = mbedtls_pk_parse_public_key(&pk, public_key.data(), public_key.size()) == 0
        && mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY
        && mbedtls_pk_ec(pk)->grp.id == MBEDTLS_ECP_DP_SECP256R1
        && mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest.data(), digest.size(),
            signature.data(), signature.size()) == 0;
    mbedtls_pk_free(&pk);
    return valid;
}

} // namespace nxloader::self_update
