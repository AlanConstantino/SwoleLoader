#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Release selection for over-the-air updates. This decides whether a GitHub
// release is newer than the running app and which of its files to download.
// Downloading, signature checks and replacing the app happen in main_menu.cpp.
namespace nxloader::self_update {

// Largest accepted app and signature downloads.
inline constexpr std::uint64_t MaxAppBytes = 64ull << 20;
inline constexpr std::uint64_t MaxSignatureBytes = 1024;
inline constexpr std::string_view SignatureSuffix = ".sig";

namespace detail {

inline char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i{}; i < a.size(); ++i) {
        if (Lower(a[i]) != Lower(b[i])) return false;
    }
    return true;
}

inline bool EndsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && EqualsIgnoreCase(value.substr(value.size() - suffix.size()), suffix);
}

} // namespace detail

// Packs "1.2.3" or "v1.2.3" the same way as libnx's MAKEHOSVERSION. Missing
// parts count as zero. Anything else, including pre-release suffixes such as
// "1.2.3-beta", gives no value.
inline std::optional<std::uint32_t> ParseVersion(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }

    std::uint32_t parts[3]{};
    for (std::size_t count{}; count < 3; ++count) {
        std::uint32_t value{};
        std::size_t digits{};
        while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
            value = value * 10 + static_cast<std::uint32_t>(text.front() - '0');
            if (value > 255) return {};
            text.remove_prefix(1);
            ++digits;
        }
        if (!digits) return {};
        parts[count] = value;
        if (text.empty()) break;
        if (text.front() != '.' || text.size() == 1 || count == 2) return {};
        text.remove_prefix(1);
    }
    return parts[0] << 16 | parts[1] << 8 | parts[2];
}

struct ReleaseAsset {
    std::string name;
    std::string url;
    std::uint64_t size{};
};

struct Release {
    std::string tag;
    std::string notes;
    std::vector<ReleaseAsset> assets;
};

enum class Status {
    UpToDate,
    Available,
    Invalid,
};

struct Check {
    Status status{Status::Invalid};
    std::uint32_t version{};
    ReleaseAsset app;
    ReleaseAsset signature;
    // Set when status is Invalid.
    std::string_view error;
};

// Compares a release with the running version and picks its files. The app
// file is the asset named preferred_nro, or else the release's only .nro
// asset, so a renamed .nro still updates older installs. Its signature is the
// asset with the same name plus ".sig".
inline Check Evaluate(const Release& release, std::uint32_t current_version, std::string_view preferred_nro) {
    Check check;
    const auto version = ParseVersion(release.tag);
    if (!version) {
        check.error = "The latest release tag is not a version number like v1.2.3.";
        return check;
    }
    check.version = *version;
    if (*version <= current_version) {
        check.status = Status::UpToDate;
        return check;
    }

    const ReleaseAsset* app{};
    const ReleaseAsset* only_nro{};
    std::size_t nro_count{};
    for (const auto& asset : release.assets) {
        if (detail::EqualsIgnoreCase(asset.name, preferred_nro)) {
            app = &asset;
        }
        if (detail::EndsWithIgnoreCase(asset.name, ".nro")) {
            only_nro = &asset;
            ++nro_count;
        }
    }
    if (!app && nro_count == 1) {
        app = only_nro;
    }
    if (!app) {
        check.error = nro_count
            ? "The latest release has several .nro files and none has the expected name."
            : "The latest release has no .nro file.";
        return check;
    }

    const ReleaseAsset* signature{};
    for (const auto& asset : release.assets) {
        if (asset.name.size() == app->name.size() + SignatureSuffix.size()
            && detail::EqualsIgnoreCase(std::string_view{asset.name}.substr(0, app->name.size()), app->name)
            && detail::EndsWithIgnoreCase(asset.name, SignatureSuffix)) {
            signature = &asset;
        }
    }
    if (!signature) {
        check.error = "The latest release is not signed.";
        return check;
    }

    if (!app->size || app->size > MaxAppBytes || !signature->size || signature->size > MaxSignatureBytes) {
        check.error = "The latest release has files of an unexpected size.";
        return check;
    }
    if (!app->url.starts_with("https://") || !signature->url.starts_with("https://")) {
        check.error = "The latest release has a download link that is not HTTPS.";
        return check;
    }

    check.status = Status::Available;
    check.app = *app;
    check.signature = *signature;
    return check;
}

// GitHub's endpoint for the newest published release, excluding drafts and
// pre-releases. repo is "owner/repo".
inline std::string LatestReleaseUrl(std::string_view repo) {
    return "https://api.github.com/repos/" + std::string{repo} + "/releases/latest";
}

} // namespace nxloader::self_update
