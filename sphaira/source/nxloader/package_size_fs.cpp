#include "nxloader/package_size_fs.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include <mutex>

namespace sphaira::nxloader_ui {

std::uint64_t ReadOriginalContentSize(std::span<const std::uint8_t> bytes) {
    static std::mutex mutex;
    static keys::Keys header_keys;
    static bool ready{};
    std::lock_guard lock(mutex);
    if (bytes.size() < sizeof(nca::Header)) return 0;
    if (!ready) {
        if (R_FAILED(splCryptoInitialize())) return 0;
        const auto rc = keys::parse_keys(header_keys, false);
        splCryptoExit();
        if (R_FAILED(rc)) return 0;
        ready = true;
    }
    nca::Header header{};
    return R_SUCCEEDED(nca::DecryptHeader(bytes.data(), header_keys, header)) ? header.size : 0;
}

}
