#pragma once

#include "fs.hpp"

namespace sphaira::nxloader_ui {
template<class Cancelled>
nxloader::PackageSize ReadPackageSize(fs::Fs* source, const std::string& path, Cancelled&& cancelled) {
    if (cancelled()) return {.error = "cancelled"};
    return static_cast<fs::SizeFs*>(source)->ReadPackage(path);
}
}
