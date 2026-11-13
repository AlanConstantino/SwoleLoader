#include "nro.hpp"
#include "nacp_compat.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "app.hpp"
#include "log.hpp"

#include <switch.h>
#include <vector>
#include <cstring>
#include <string_view>
#include <minIni.h>

namespace sphaira {
namespace {

auto nro_get_abi_revision(fs::File& file, const NroData& data) -> u32 {
    struct {
        u32 magic;
        u32 revision;
    } abi{};

    // The LNY2 ABI marker follows the module header, as checked by hbmenu.
    const u64 offset = u64(data.start.mod_offset) + 0x34;
    if (offset + sizeof(abi) > data.header.size) {
        return 0;
    }

    u64 bytes_read{};
    if (R_FAILED(file.Read(offset, &abi, sizeof(abi), FsReadOption_None, &bytes_read)) ||
        bytes_read != sizeof(abi) || abi.magic != 0x32594E4C) {
        return 0;
    }

    return abi.revision;
}

auto nro_parse_internal(fs::Fs* fs, const fs::FsPath& path, NroEntry& entry) -> Result {
    entry.path = path;

    // todo: special sorting for fw 2.0.0 to make it not look like shit
    if (hosversionAtLeast(3,0,0)) {
        // it doesn't matter if we fail
        entry.timestamp.is_valid = false;
        fs->GetFileTimeStampRaw(entry.path, &entry.timestamp);
        // if (R_FAILED(fsFsGetFileTimeStampRaw(fs, entry.path, &entry.timestamp))) {
        //     // log_write("failed to get timestamp for: %s\n", path);
        // }
    }

    fs::File f;
    R_TRY(fs->OpenFile(entry.path, FsOpenMode_Read, &f));

    // todo: buffer reads to 16k to avoid 2 fs read calls per entry.
    NroData data{};
    u64 bytes_read;
    R_TRY(f.Read(0, &data, sizeof(data), FsReadOption_None, &bytes_read));
    R_UNLESS(bytes_read == sizeof(data), Result_NroBadSize);
    R_UNLESS(data.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);
    entry.abi_revision = nro_get_abi_revision(f, data);

    NroAssetHeader asset{};
    R_TRY(f.Read(data.header.size, &asset, sizeof(asset), FsReadOption_None, &bytes_read));
    // R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, Result_NroBadMagic);

    // we can avoid a GetSize() call by calculating the size manually.
    entry.size = data.header.size;

    // some .nro (vgedit) have bad nacp, fake the nacp
    auto& nacp = entry.nacp;
    if (asset.magic != NROASSETHEADER_MAGIC || asset.nacp.offset == 0 || asset.nacp.size != sizeof(NacpStruct)) {
        std::memset(&asset, 0, sizeof(asset));
        std::memset(&nacp, 0, sizeof(nacp));

        // get the name without the .nro
        const auto file_name = std::strrchr(path, '/') + 1;
        const auto file_name_len = std::strlen(file_name);
        std::strncpy(nacp.lang.name, file_name, file_name_len - 4);
        std::strcpy(nacp.lang.author, "Unknown");
        std::strcpy(nacp.display_version, "Unknown");

        entry.icon_offset = entry.icon_size = 0;
        entry.is_nacp_valid = false;
    } else {
        entry.size += sizeof(asset) + asset.icon.size + asset.nacp.size + asset.romfs.size;
        R_TRY(f.Read(data.header.size + asset.nacp.offset, &nacp.lang, sizeof(nacp.lang), FsReadOption_None, &bytes_read));
        R_TRY(f.Read(data.header.size + asset.nacp.offset + offsetof(NacpStruct, display_version), nacp.display_version, sizeof(nacp.display_version), FsReadOption_None, &bytes_read));

        // lazy load the icons
        entry.icon_size = asset.icon.size;
        entry.icon_offset = data.header.size + asset.icon.offset;
        entry.is_nacp_valid = true;
    }

    R_SUCCEED();
}

auto launch_internal(const std::string& path, const std::string& argv) -> Result {
    R_TRY(envSetNextLoad(path.c_str(), argv.c_str()));

    log_write("set launch with path: %s argv: %s\n", path.c_str(), argv.c_str());

    evman::push(evman::LaunchNroEventData{path, argv});
    R_SUCCEED();
}

} // namespace

/*
    NRO INFO PAGE:
    - icon
    - name
    - author
    - path
    - filesize
    - launch count
    - timestamp created
    - timestamp modified
*/

auto nro_parse(const fs::FsPath& path, NroEntry& entry) -> Result {
    fs::FsNativeSd fs;
    return nro_parse_internal(&fs, path, entry);
}

auto nro_get_nacp(const fs::FsPath& path, NacpStruct& nacp) -> Result {
    fs::FsNativeSd fs;
    NroData data;
    NroAssetHeader asset;
    u64 bytes_read;

    fs::File f;
    R_TRY(fs.OpenFile(path, FsOpenMode_Read, &f));

    R_TRY(f.Read(0, &data, sizeof(data), FsReadOption_None, &bytes_read));
    R_UNLESS(data.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);
    R_TRY(f.Read(data.header.size, &asset, sizeof(asset), FsReadOption_None, &bytes_read));
    R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, Result_NroBadMagic);
    R_TRY(f.Read(data.header.size + asset.nacp.offset, &nacp, sizeof(nacp), FsReadOption_None, &bytes_read));

    R_SUCCEED();
}

auto nro_launch(std::string path, std::string args) -> Result {
    if (path.empty()) {
        return 1;
    }

    // keeps compat with hbloader
    // https://github.com/ITotalJustice/Gamecard-Installer-NX/blob/master/source/main.c#L73
    // https://github.com/ITotalJustice/Gamecard-Installer-NX/blob/d549c5f916dea814fa0a7e5dc8c903fa3044ba15/source/main.c#L29
    if (!path.starts_with("sdmc:")) {
        path = "sdmc:" + path;
    }

    if (args.empty()) {
        args = nro_add_arg(path);
    } else {
        args = nro_add_arg(path) + ' ' + args;
    }

    return launch_internal(path, args);
}

auto nro_add_arg(std::string arg) -> std::string {
    if (arg.contains(' ')) {
        return '\"' + arg + '\"';
    }
    return arg;
}

} // namespace sphaira
