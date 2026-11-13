#pragma once
#include "fs.hpp"
#include <vector>

namespace sphaira::ui::menu::filebrowser {
enum { SortType_Size, SortType_Alphabetical };
enum { OrderType_Descending, OrderType_Ascending };
struct FileEntry : FsDirectoryEntry {
    FsTimeStampRaw time_stamp{};
    bool selected{};
    bool done_stat{};
    bool IsDir() const { return type == FsDirEntryType_Dir; }
    bool IsFile() const { return type == FsDirEntryType_File; }
    bool IsHidden() const { return name[0] == '.'; }
};
}
