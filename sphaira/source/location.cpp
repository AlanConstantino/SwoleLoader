#include "location.hpp"
#include "fs.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <cstring>

#ifdef ENABLE_LIBUSBHSFS
    #include <usbhsfs.h>
#endif // ENABLE_LIBUSBHSFS

namespace sphaira::location {
namespace {

} // namespace

auto GetStdio(bool write) -> StdioEntries {
    StdioEntries out{};

#ifdef ENABLE_LIBUSBHSFS
    // bail out early if usbhdd is disabled.
    if (!App::GetHddEnable()) {
        log_write("[USBHSFS] not enabled\n");
        return out;
    }

    static UsbHsFsDevice devices[0x20];
    const auto count = usbHsFsListMountedDevices(devices, std::size(devices));
    log_write("[USBHSFS] got connected: %u\n", usbHsFsGetPhysicalDeviceCount());
    log_write("[USBHSFS] got count: %u\n", count);

    for (s32 i = 0; i < count; i++) {
        const auto& e = devices[i];

        if (write && (e.write_protect || (e.flags & UsbHsFsMountFlags_ReadOnly))) {
            log_write("[USBHSFS] skipping write protect\n");
            continue;
        }

        char display_name[0x100];
        std::snprintf(display_name, sizeof(display_name), "%s (%s - %s - %zu GB)", e.name, LIBUSBHSFS_FS_TYPE_STR(e.fs_type), e.product_name, e.capacity / 1024 / 1024 / 1024);

        u32 flags = 0;
        if (e.write_protect || (e.flags & UsbHsFsMountFlags_ReadOnly)) {
            flags |= FsEntryFlag::FsEntryFlag_ReadOnly;
        }

        out.emplace_back(e.name, display_name, flags);
        log_write("\t[USBHSFS] %s name: %s serial: %s man: %s\n", e.name, e.product_name, e.serial_number, e.manufacturer);
    }
#endif // ENABLE_LIBUSBHSFS

    return out;
}

} // namespace sphaira::location
