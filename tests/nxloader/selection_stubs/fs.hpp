#pragma once

#include "../browser_stubs/fs.hpp"
#include "nxloader/package_size.hpp"

namespace fs {
struct SizeFs : Fs {
    virtual nxloader::PackageSize ReadPackage(const std::string& path) = 0;
};
}
