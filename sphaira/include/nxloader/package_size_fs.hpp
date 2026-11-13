#pragma once

#include "fs.hpp"
#include "nxloader/package_size.hpp"

#include <limits>
#include <utility>

namespace sphaira::nxloader_ui {

std::uint64_t ReadOriginalContentSize(std::span<const std::uint8_t> header);

template<class Cancelled>
nxloader::PackageSize ReadPackageSize(fs::Fs* source, const std::string& path, Cancelled&& cancelled) {
    nxloader::PackageSize result;
    result.source_known = false;
    if (cancelled()) {
        result.error = "Size calculation cancelled.";
        return result;
    }
    if (!source || path.empty() || path.size() >= sizeof(fs::FsPath)) {
        result.error = "Invalid package path.";
        return result;
    }
    for (unsigned attempt = 0; attempt < 2 && !cancelled(); ++attempt) {
        fs::File file;
        if (R_FAILED(source->OpenFile(path, FsOpenMode_Read, &file))) {
            s64 size{};
            FsTimeStampRaw stamp{};
            if (R_SUCCEEDED(source->FileGetSizeAndTimestamp(path, &stamp, &size)) && size >= 0) {
                result.source_bytes = size;
                result.source_known = true;
            }
            result.error = "Could not open package metadata (drive or file read failed).";
            continue;
        }
        s64 size{};
        if (R_FAILED(file.GetSize(&size)) || size < 0) {
            result.error = "Could not read package size.";
            continue;
        }
        bool read_failed{};
        result = nxloader::EstimatePackageSize(path, static_cast<std::uint64_t>(size),
        [&](std::uint64_t offset, std::span<std::uint8_t> output) {
            if (cancelled() || offset > static_cast<std::uint64_t>(size)
                || output.size() > static_cast<std::uint64_t>(size) - offset
                || offset > static_cast<std::uint64_t>(std::numeric_limits<s64>::max())) return false;
            while (!output.empty() && !cancelled()) {
                u64 read{};
                if (R_FAILED(file.Read(static_cast<s64>(offset), output.data(), output.size(), 0, &read))
                    || !read || read > output.size()) { read_failed = true; return false; }
                offset += read;
                output = output.subspan(read);
            }
            return output.empty();
        }, cancelled, ReadOriginalContentSize);
        if (!read_failed) break;
    }
    return result;
}

}
