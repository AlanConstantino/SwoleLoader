// Usage: update_signature_test public.der other_public.der rsa_public.der file file.sig
// The signature is made with `openssl dgst -sha256 -sign` by run_self_update.sh.
#include "nxloader/update_signature.hpp"

#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

using namespace nxloader::self_update;

namespace {

int failures{};

void Expect(bool condition, const char* text, int line) {
    if (!condition) {
        std::fprintf(stderr, "line %d: %s\n", line, text);
        ++failures;
    }
}

#define EXPECT(x) Expect((x), #x, __LINE__)

std::vector<std::uint8_t> Read(const char* path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, {}};
}

Sha256 Hash(const std::vector<std::uint8_t>& data) {
    Sha256 digest{};
    mbedtls_sha256_ret(data.data(), data.size(), digest.data(), 0);
    return digest;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: %s public.der other_public.der rsa_public.der file file.sig\n", argv[0]);
        return EXIT_FAILURE;
    }
    const auto key = Read(argv[1]);
    const auto other_key = Read(argv[2]);
    const auto rsa_key = Read(argv[3]);
    auto data = Read(argv[4]);
    auto signature = Read(argv[5]);
    if (key.empty() || other_key.empty() || rsa_key.empty() || data.empty() || signature.empty()) {
        std::fputs("missing test input\n", stderr);
        return EXIT_FAILURE;
    }

    const auto digest = Hash(data);
    EXPECT(VerifySignature(key, digest, signature));

    // Wrong key, wrong key type, missing key.
    EXPECT(!VerifySignature(other_key, digest, signature));
    EXPECT(!VerifySignature(rsa_key, digest, signature));
    EXPECT(!VerifySignature({}, digest, signature));
    EXPECT(!VerifySignature(std::span{key}.first(key.size() - 1), digest, signature));

    // A single changed byte anywhere in the file.
    for (const std::size_t at : {std::size_t{0}, data.size() / 2, data.size() - 1}) {
        data[at] ^= 1;
        EXPECT(!VerifySignature(key, Hash(data), signature));
        data[at] ^= 1;
    }

    // Damaged, truncated, extended and empty signatures.
    for (std::size_t at{}; at < signature.size(); ++at) {
        signature[at] ^= 0x80;
        if (VerifySignature(key, digest, signature)) {
            std::fprintf(stderr, "signature with byte %zu changed was accepted\n", at);
            ++failures;
        }
        signature[at] ^= 0x80;
    }
    EXPECT(!VerifySignature(key, digest, std::span{signature}.first(signature.size() - 1)));
    auto extended = signature;
    extended.push_back(0);
    EXPECT(!VerifySignature(key, digest, extended));
    EXPECT(!VerifySignature(key, digest, {}));
    EXPECT(VerifySignature(key, digest, signature));

    if (failures) {
        std::fprintf(stderr, "%d signature check(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("Update signature tests passed: valid signature, wrong keys, changed files and damaged signatures.");
}
