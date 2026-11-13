#pragma once
#include "fs.hpp"
namespace sphaira::nxloader_ui {
template<class Emit>
std::string ReadDirectory(fs::Fs* source, const std::string& path, Emit emit) {
    return source->ReadDirectory(path, emit);
}
}
