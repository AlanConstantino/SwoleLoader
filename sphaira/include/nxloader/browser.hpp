#pragma once

#include "ui/menus/filebrowser.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sphaira::nxloader_ui {

class BrowserLoader {
public:
    struct Request {
        std::shared_ptr<fs::Fs> source;
        fs::FsPath path;
        std::vector<std::string> filter;
        long sort{ui::menu::filebrowser::SortType_Alphabetical};
        long order{ui::menu::filebrowser::OrderType_Descending};
        bool folders_first{true};
        bool hidden_last{};
        bool reuse_entries{};
        std::vector<ui::menu::filebrowser::FileEntry> recycled_entries;
    };

    struct Snapshot {
        std::vector<ui::menu::filebrowser::FileEntry> entries;
        std::vector<u32> visible_indices;
        std::vector<u32> all_indices;
        std::string error;
        size_t selected_count{};
    };

    BrowserLoader();
    ~BrowserLoader();
    BrowserLoader(const BrowserLoader&) = delete;
    BrowserLoader& operator=(const BrowserLoader&) = delete;

    // Called from the UI thread. Superseded work is cancelled without waiting
    // for disk I/O; Poll starts the latest queued request when it can run.
    void Submit(Request request);
    void Cancel();
    std::unique_ptr<Snapshot> Poll();
    bool IsLoading() const;
    size_t Count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
