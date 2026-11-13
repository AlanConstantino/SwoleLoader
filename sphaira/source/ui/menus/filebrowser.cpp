#include "ui/menus/filebrowser.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/image_viewer.hpp"

#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "utils/utils.hpp"

#include "log.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
#include "nacp_compat.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "download.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "hasher.hpp"
#include "location.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "title_info.hpp"

#include "yati/yati.hpp"
#include "yati/source/file.hpp"
#include "nxloader/install.hpp"
#include "nxloader/browser.hpp"
#include "nxloader/capacity.hpp"

#include <minIni.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <dirent.h>
#include <cstring>
#include <cassert>
#include <string>
#include <string_view>
#include <ctime>
#include <span>
#include <utility>
#include <ranges>
#include <charconv>

namespace sphaira::ui::menu::filebrowser {
namespace {

std::atomic_bool g_change_signalled{};

constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_IsSd,
};

constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
};

constexpr std::string_view AUDIO_EXTENSIONS[] = {
    "mp3", "ogg", "flac", "wav", "aac", "ac3", "aif", "asf", "bfwav",
    "bfsar", "bfstm", "bwav",
};
constexpr std::string_view VIDEO_EXTENSIONS[] = {
    "mp4", "mkv", "m3u", "m3u8", "hls", "vob", "avi", "dv", "flv", "m2ts", "webm",
    "m2v", "m4a", "mov", "mpeg", "mpg", "mts", "swf", "ts", "vob", "wma", "wmv",
};
constexpr std::string_view IMAGE_EXTENSIONS[] = {
    "png", "jpg", "jpeg", "bmp", "gif",
};
constexpr std::string_view INSTALL_EXTENSIONS[] = {
    "nsp", "xci", "nsz", "xcz", "msp",
};
// these are files that are already compressed or encrypted and should
// be stored raw in a zip file.
constexpr std::string_view COMPRESSED_EXTENSIONS[] = {
    "zip", "xz", "7z", "rar", "tar", "nca", "nsp", "xci", "nsz", "xcz"
};
constexpr std::string_view ZIP_EXTENSIONS[] = {
    "zip",
};
constexpr std::string_view ATMOSPHERE_CONTENTS_PATH{"/atmosphere/contents"};

auto IsAtmosphereContentsPath(std::string_view path) -> bool {
    while (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }

    return path.size() == ATMOSPHERE_CONTENTS_PATH.size() &&
        !strncasecmp(path.data(), ATMOSPHERE_CONTENTS_PATH.data(), path.size());
}

auto ParseApplicationId(std::string_view name, u64& application_id) -> bool {
    if (name.size() != 16) {
        return false;
    }

    const auto end = name.data() + name.size();
    const auto result = std::from_chars(name.data(), end, application_id, 16);
    return result.ec == std::errc{} && result.ptr == end && application_id != 0;
}

} // namespace

// case insensitive check
auto IsSamePath(std::string_view a, std::string_view b) -> bool {
    return a.length() == b.length() && !strncasecmp(a.data(), b.data(), a.length());
}

auto IsExtension(std::string_view ext1, std::string_view ext2) -> bool {
    return ext1.length() == ext2.length() && !strncasecmp(ext1.data(), ext2.data(), ext1.length());
}

auto IsExtension(std::string_view ext, std::span<const std::string_view> list) -> bool {
    for (auto e : list) {
        if (IsExtension(e, ext)) {
            return true;
        }
    }
    return false;
}

void SignalChange() {
    g_change_signalled = true;
}

FsView::FsView(Base* menu, const std::shared_ptr<fs::Fs>& fs, const fs::FsPath& path, const FsEntry& entry, ViewSide side) : m_menu{menu}, m_side{side} {
    if (m_menu->m_options & FsOption_CanSelect) {
        const auto toggle = [this] { ToggleSelection(); };
        SetAction(Button::L2, Action{toggle});
        SetAction(Button::R2, Action{toggle});
        SetAction(Button::R3, Action{"Select (ZL/ZR)", toggle});
    }

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (m_entries_current.empty()) {
                return;
            }

            m_menu->OnClick(this, m_fs_entry, GetEntry(), GetNewPathCurrent());
        }}),

        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            std::string_view view{m_path};
            if (view != m_fs->Root()) {
                const auto end = view.find_last_of('/');
                assert(end != view.npos);

                if (end == 0) {
                    Scan(m_fs->Root(), true);
                } else {
                    Scan(view.substr(0, end), true);
                }
            } else {
                if (!m_menu->IsTab()) {
                    m_menu->PromptIfShouldExit();
                }
            }
        }}),

        std::make_pair(Button::X, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    log_write("setting side\n");
    SetSide(m_side);

    log_write("getting path\n");
    auto buf = path;
    if (path.empty() && entry.IsSd()) {
        ini_gets("paths", "last_path", entry.root, buf, sizeof(buf), App::CONFIG_PATH);
    }

    // in case the above fails.
    if (buf.empty()) {
        buf = entry.root;
    }

    log_write("setting fs\n");
    SetFs(fs, buf, entry);
    log_write("set fs\n");
}

FsView::FsView(FsView* view, ViewSide side) : FsView{view->m_menu, view->m_fs, view->m_path, view->m_fs_entry, side} {

}

FsView::FsView(Base* menu, ViewSide side) : FsView{menu, menu->CreateFs(FS_ENTRY_DEFAULT), {}, FS_ENTRY_DEFAULT, side} {

}

FsView::~FsView() {
    m_loader.reset();
    if (m_title_info_initialized) {
        title::Exit();
    }

    // don't store mount points for non-sd card paths.
    if (IsSd() && !m_entries_current.empty()) {
        ini_puts("paths", "last_path", m_path, App::CONFIG_PATH);
        ini_puts("paths", "last_file", GetEntry().name, App::CONFIG_PATH);
    }
}

void FsView::Update(Controller* controller, TouchInfo* touch) {
    auto navigation = *controller;
    navigation.m_kheld &= ~static_cast<u64>(Button::R2);
    m_list->OnUpdate(&navigation, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect::Focus);
            SetIndex(i);
        }
    });
}

bool FsView::IsSelected(const FileEntry& entry) const {
    return m_menu->m_cart->Selected(m_selection_key, GetNewPath(entry).toString());
}

void FsView::ToggleSelection() {
    if (m_entries_current.empty()) return;
    m_menu->ResetSelection();
    QueueSize(m_entries_current[m_index], !IsSelected(GetEntry()));
    m_menu->UpdateSubheading();
}

void FsView::SelectAllVisible() {
    m_menu->ResetSelection();
    std::vector<nxloader::Root> roots;
    roots.reserve(m_entries_current.size());
    for (const auto index : m_entries_current) {
        const auto& entry = m_entries[index];
        roots.push_back({GetNewPath(entry).toString(), entry.IsDir() ? nxloader::EntryKind::Directory : nxloader::EntryKind::File});
    }
    m_menu->m_cart->Queue(m_selection_key, m_fs, std::move(roots));
}

void FsView::ClearSelection() {
    m_menu->ResetSelection();
    m_menu->m_cart->Clear();
    m_menu->m_review_after_selection = false;
    m_menu->UpdateSubheading();
}

void FsView::QueueSize(u32 index, bool selected) {
    auto& entry = m_entries[index];
    const auto path = GetNewPath(entry).toString();
    m_menu->m_cart->Set(m_selection_key, m_fs,
        {path, entry.IsDir() ? nxloader::EntryKind::Directory : nxloader::EntryKind::File}, selected);
    entry.selected = selected;
}

void FsView::PollSizes() {
    if (m_loader && m_loader->IsLoading()) return;
    if (m_selection_revision != m_menu->m_cart->Revision()) {
        m_selection_revision = m_menu->m_cart->Revision();
        m_selection_sync_cursor = 0;
        m_selected_count = 0;
    }
    const auto end = std::min(m_entries.size(), m_selection_sync_cursor + 64);
    for (; m_selection_sync_cursor < end; ++m_selection_sync_cursor) {
        auto& entry = m_entries[m_selection_sync_cursor];
        entry.selected = IsSelected(entry);
        m_selected_count += entry.selected;
    }
}

void FsView::Draw(NVGcontext* vg, Theme* theme) {
    const auto& text_col = theme->GetColour(ThemeEntryID_TEXT);

    if (m_entries_current.empty()) {
        const auto message = m_loader && m_loader->IsLoading()
            ? "Loading folder... " + std::to_string(m_loader->Count()) + " entries"
            : m_load_error.empty() ? "Empty folder" : "Unable to read folder. Press B to go back.";
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 26.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", message.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_entries_current.size(), [this, text_col](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_entries_current.size() - 1) {
                gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        if (e.IsDir()) {
            DrawElement(x + text_xoffset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
        } else {
            auto icon = ThemeEntryID_ICON_FILE;
            const auto ext = e.GetExtension();
            if (IsExtension(ext, AUDIO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_AUDIO;
            } else if (IsExtension(ext, VIDEO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_VIDEO;
            } else if (IsExtension(ext, IMAGE_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_IMAGE;
            } else if (IsExtension(ext, INSTALL_EXTENSIONS)) {
                // todo: maybe replace this icon with something else?
                icon = ThemeEntryID_ICON_NRO;
            } else if (IsExtension(ext, ZIP_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_ZIP;
            } else if (IsExtension(ext, "nro")) {
                icon = ThemeEntryID_ICON_NRO;
            }

            DrawElement(x + text_xoffset, y + 5, 50, 50, icon);
        }

        const bool included = IsSelected(e);
        const bool partial_selection = e.IsDir() && m_menu->m_cart->Partial(m_selection_key, GetNewPath(e).toString());
        if (included || partial_selection) {
            gfx::drawText(vg, x + text_xoffset + 50 / 2, y + (h / 2.f) - (24.f / 2), 24.f, partial_selection ? "-" : "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        }

        if (e.application_id && !e.title_lookup_complete) {
            if (const auto data = title::GetAsync(e.application_id)) {
                e.title_lookup_complete = true;
                if (data->status == title::NacpLoadStatus::Loaded && data->lang.name[0]) {
                    e.display_name = e.name;
                    e.display_name += " - ";
                    e.display_name += data->lang.name;
                }
            }
        }

        const auto size_width = included ? (m_menu->IsSplitScreen() ? 170.f : 265.f) : 125.f;
        m_scroll_name.Draw(vg, selected, x + text_xoffset+65, y + (h / 2.f), w-(text_xoffset+65+size_width), 20, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), e.GetDisplayName());

        if (included) {
            const auto found = m_menu->m_cart->Size(m_selection_key, GetNewPath(e).toString());
            if (!found) {
                gfx::drawText(vg, x + w - text_xoffset, y + h / 2.f, 15.f,
                    (e.IsDir() ? "Included in folder total" : "Calculating size..."), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO));
            } else {
                const auto& size = *found;
                const auto partial = !size.complete || size.unknown_count;
                const auto source_text = std::string("Files ") + (!size.complete || size.source_unknown_count ? ">=" : "") + utils::formatSizeStorage(size.source_bytes);
                const auto install_text = size.unknown_count ? std::string("Install >=") + utils::formatSizeStorage(size.installed_bytes) + " + ?"
                    : std::string("Install ") + (partial ? ">=" : "~") + utils::formatSizeStorage(size.installed_bytes);
                gfx::drawText(vg, x + w - text_xoffset, y + 17.f, 14.f, source_text.c_str(), nullptr,
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO));
                gfx::drawText(vg, x + w - text_xoffset, y + 41.f, 15.f, install_text.c_str(), nullptr,
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(size.unknown_count ? ThemeEntryID_ERROR : ThemeEntryID_TEXT));
            }
        } else if (e.IsDir()) {
            gfx::drawText(vg, x + w - text_xoffset, y + h / 2.f, 16.f,
                "Folder", nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO));
        } else if (e.file_size >= 0) {
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", utils::formatSizeStorage(e.file_size).c_str());
        }
    });
}

void FsView::OnFocusGained() {
    Widget::OnFocusGained();
    if (!m_scanned) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }

        if (IsSd()) {
            LastFile last_file{};
            if (ini_gets("paths", "last_file", "", last_file.name, sizeof(last_file.name), App::CONFIG_PATH)) {
                m_restore_after_load = last_file;
            }
        }
    }
}

void FsView::SetSide(ViewSide side) {
    m_side = side;

    const auto pos = m_menu->GetPos();
    this->SetPos(pos);
    Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 45.f * 2, 60};

    if (m_menu->IsSplitScreen()) {
        if (m_side == ViewSide::Left) {
            this->SetW(pos.w / 2 - pos.x / 2);
            this->SetX(pos.x / 2 + 20.f);
        } else if (m_side == ViewSide::Right) {
            this->SetW(pos.w / 2 - pos.x / 2);
            this->SetX(pos.x / 2 + SCREEN_WIDTH / 2);
        }

        v.w /= 2;
        v.w -= v.x / 2;

        if (m_side == ViewSide::Left) {
            v.x = v.x / 2 + 20.f;
        } else if (m_side == ViewSide::Right) {
            v.x = v.x / 2 + SCREEN_WIDTH / 2;
        }
    }

    m_list = std::make_unique<List>(1, 8, m_pos, v);
    if (m_menu->IsSplitScreen()) {
        m_list->SetPageJump(false);
    }

    // reset scroll position.
    m_scroll_name.Reset();
}

void FsView::OnClick() {
    if (m_entries_current.empty()) return;

    const auto& entry = GetEntry();

    if (entry.type == FsDirEntryType_Dir) {
        Scan(GetNewPathCurrent());
    } else if (IsExtension(entry.GetExtension(), INSTALL_EXTENSIONS)) {
        InstallFiles();
    } else if (IsExtension(entry.GetExtension(), IMAGE_EXTENSIONS)) {
        App::Push<imageview::Menu>(GetFs(), GetNewPathCurrent());
    }
}

void FsView::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff();
    }


    m_menu->UpdateSubheading();
}

void FsView::InstallFiles() {
    if (m_menu->m_cart->Queued()) {
        m_menu->m_review_after_selection = true;
        App::Notify("Finishing bulk selection; Review will open when ready.");
        App::PopToMenu();
        return;
    }
    if (!m_menu->m_cart->Count() && !m_entries_current.empty()) QueueSize(m_entries_current[m_index], true);
    nxloader_ui::ScanAndInstall(m_menu->m_cart);
}

void FsView::UnzipFiles(fs::FsPath dir_path) {
    const auto targets = GetSelectedEntries();

    // set to current path.
    if (dir_path.empty()) {
        dir_path = m_path;
    }

    App::Push<ui::ProgressBox>(0, "Extracting "_i18n, "", [this, dir_path, targets](auto pbox) -> Result {
        const auto is_hdd_fs = m_fs->Root().starts_with("ums");

        for (auto& e : targets) {
            pbox->SetTitle(e.GetName());
            const auto zip_out = GetNewPath(e);
            R_TRY(thread::TransferUnzipAll(pbox, zip_out, m_fs.get(), dir_path, nullptr, is_hdd_fs ? thread::Mode::SingleThreaded : thread::Mode::SingleThreadedIfSmaller));
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Extract failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Extract success!"_i18n);
        }

        Scan(m_path);
        log_write("did extract\n");
    });
}

void FsView::ZipFiles(fs::FsPath zip_out) {
    const auto targets = GetSelectedEntries();

    // set to current path.
    if (zip_out.empty()) {
        if (std::size(targets) == 1) {
            const auto name = targets[0].name;
            const auto ext = std::strrchr(targets[0].name, '.');
            fs::FsPath file_path;
            if (!ext) {
                std::snprintf(file_path, sizeof(file_path), "%s.zip", name);
            } else {
                std::snprintf(file_path, sizeof(file_path), "%.*s.zip", (int)(ext - name), name);
            }
            zip_out = fs::AppendPath(m_path, file_path);
            log_write("zip out: %s name: %s file_path: %s\n", zip_out.s, name, file_path.s);
        } else {
            // loop until we find an unused file name.
            for (u64 i = 0; ; i++) {
                fs::FsPath file_path = "Archive.zip";
                if (i) {
                    std::snprintf(file_path, sizeof(file_path), "Archive (%zu).zip", i);
                }

                zip_out = fs::AppendPath(m_path, file_path);
                if (!m_fs->FileExists(zip_out)) {
                    break;
                }
            }
        }
    } else {
        if (!std::string_view(zip_out).ends_with(".zip")) {
            zip_out += ".zip";
        }
    }

    App::Push<ui::ProgressBox>(0, "Compressing "_i18n, "", [this, zip_out, targets](auto pbox) -> Result {
        const auto t = std::time(NULL);
        const auto tm = std::localtime(&t);
        const auto is_hdd_fs = m_fs->Root().starts_with("ums");

        // pre-calculate the time rather than calculate it in the loop.
        zip_fileinfo zip_info{};
        zip_info.tmz_date.tm_sec = tm->tm_sec;
        zip_info.tmz_date.tm_min = tm->tm_min;
        zip_info.tmz_date.tm_hour = tm->tm_hour;
        zip_info.tmz_date.tm_mday = tm->tm_mday;
        zip_info.tmz_date.tm_mon = tm->tm_mon;
        zip_info.tmz_date.tm_year = tm->tm_year;

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);

        auto zfile = zipOpen2_64(zip_out, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        ON_SCOPE_EXIT(zipClose(zfile, APP_NAME " v" APP_DISPLAY_VERSION));

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            // the file name needs to be relative to the current directory.
            const char* file_name_in_zip = file_path.s + std::strlen(m_path);

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, m_fs->Root(), std::strlen(m_fs->Root()))) {
                file_name_in_zip += std::strlen(m_fs->Root());
            }

            // root paths are banned in zips, they will warn when extracting otherwise.
            while (file_name_in_zip[0] == '/') {
                file_name_in_zip++;
            }

            pbox->NewTransfer(file_name_in_zip);

            if (ZIP_OK != zipOpenNewFileInZip(zfile, file_name_in_zip, &zip_info, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION)) {
                log_write("failed to add zip for %s\n", file_path.s);
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));

            return thread::TransferZip(pbox, zfile, m_fs.get(), file_path, nullptr, is_hdd_fs ? thread::Mode::SingleThreaded : thread::Mode::SingleThreadedIfSmaller);
        };

        for (auto& e : targets) {
            pbox->SetTitle(e.GetName());
            if (e.IsFile()) {
                const auto file_path = GetNewPath(e);
                R_TRY(zip_add(file_path));
            } else {
                FsDirCollections collections;
                get_collections(GetNewPath(e), e.name, collections);

                for (const auto& collection : collections) {
                    for (const auto& file : collection.files) {
                        const auto file_path = fs::AppendPath(collection.path, file.name);
                        R_TRY(zip_add(file_path));
                    }
                }
            }
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Compress failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Compress success!"_i18n);
        }

        Scan(m_path);
        log_write("did compress\n");
    });
}

auto FsView::Scan(fs::FsPath new_path, bool is_walk_up) -> Result {
    m_sort_pending = false;
    if (!std::strchr(new_path, '/')) {
        R_UNLESS(new_path.size() + 1 < sizeof(new_path), FsError_OutOfRange);
        new_path += '/';
    }

    m_restore_after_load.reset();
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        m_restore_after_load = m_previous_highlighted_file.back();
        m_previous_highlighted_file.pop_back();
    } else if (new_path != m_path && !m_path.empty() && !m_entries_current.empty()) {
        m_previous_highlighted_file.emplace_back(GetEntry().name, m_index,
            m_list->GetYoff(), m_entries_current.size());
    }

    g_change_signalled = false;
    m_path = new_path;
    m_selected_count = 0;
    m_menu->SetTitleSubHeading(m_path);
    StartLoad(false);
    R_SUCCEED();
}

void FsView::StartLoad(bool reuse_entries) {
    App::PopToMenu();
    m_selection_revision = ~std::uint64_t{0};
    if (!m_loader) {
        m_loader = std::make_unique<nxloader_ui::BrowserLoader>();
    }
    nxloader_ui::BrowserLoader::Request request;
    request.source = m_fs;
    request.path = m_path;
    request.filter = m_menu->m_filter;
    request.sort = m_menu->m_sort.Get();
    request.order = m_menu->m_order.Get();
    request.folders_first = m_menu->m_folders_first.Get();
    request.hidden_last = m_menu->m_hidden_last.Get();
    request.reuse_entries = reuse_entries;
    m_entries_current = {};
    request.recycled_entries = std::move(m_entries);
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();
    m_scanned = true;
    m_load_error.clear();
    m_loader->Submit(std::move(request));
    SetIndex(0);
}

void FsView::PollLoad() {
    if (!m_loader) return;
    auto result = m_loader->Poll();
    if (!result) return;
    m_entries = std::move(result->entries);
    m_entries_index = std::move(result->visible_indices);
    m_entries_index_hidden = std::move(result->all_indices);
    m_selected_count = result->selected_count;
    m_load_error = std::move(result->error);
    m_entries_current = m_menu->m_show_hidden.Get() ? std::span<u32>{m_entries_index_hidden} : std::span<u32>{m_entries_index};
    if (m_sort_pending && m_load_error.empty()) {
        m_sort_pending = false;
        StartLoad(true);
        return;
    }
    m_sort_pending = false;
    if (!m_load_error.empty()) {
        log_write("[" APP_NAME "] directory load failed: %s\n", m_load_error.c_str());
        App::Notify(m_load_error);
    }

    if (IsSd() && IsAtmosphereContentsPath(m_path)) {
        if (!m_title_info_initialized && R_SUCCEEDED(title::Init())) {
            m_title_info_initialized = true;
        }
        if (m_title_info_initialized) {
            for (auto& entry : m_entries) {
                if (entry.IsDir() && ParseApplicationId(entry.name, entry.application_id)) {
                    title::PushAsync(entry.application_id);
                }
            }
        }
    }

    SetIndex(0);
    if (m_restore_after_load) {
        SetIndexFromLastFile(*m_restore_after_load);
        m_restore_after_load.reset();
    }
}

void FsView::Sort() {
    if (m_loader && m_loader->IsLoading()) {
        m_sort_pending = true;
        return;
    }
    const auto reuse = m_scanned && m_load_error.empty();
    StartLoad(reuse);
}

void FsView::SortAndFindLastFile(bool scan) {
    std::optional<LastFile> last_file;
    if (!m_path.empty() && !m_entries_current.empty()) {
        last_file = LastFile(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
    }
    if (scan) {
        Scan(m_path);
    } else {
        Sort();
    }
    if (last_file) {
        m_restore_after_load = last_file;
    }
}

void FsView::SetIndexFromLastFile(const LastFile& last_file) {
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (last_file.name == GetEntry(i).name) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        if (index == last_file.index && m_entries_current.size() == last_file.entries_count) {
            m_list->SetYoff(last_file.offset);
            log_write("index is the same as last time\n");
        } else {
            // file position changed!
            log_write("file position changed\n");
            // guesstimate where the position is
            if (index >= 8) {
                m_list->SetYoff(((index - 8) + 1) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
        }
        SetIndex(index);
    }
}

void FsView::OnDeleteCallback() {
    bool use_progress_box{true};

    // check if we only have 1 file / folder
    if (m_menu->m_selected.m_files.size() == 1) {
        const auto& entry = m_menu->m_selected.m_files[0];
        const auto full_path = GetNewPath(m_menu->m_selected.m_path, entry.name);

        if (entry.IsDir()) {
            bool empty{};
            m_fs->IsDirEmpty(full_path, &empty);
            if (empty) {
                if (auto rc = m_fs->DeleteDirectory(full_path); R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to delete directory"_i18n);
                }
                use_progress_box = false;
            }
        } else {
            if (auto rc = m_fs->DeleteFile(full_path); R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to delete file"_i18n);
            }
            use_progress_box = false;
        }
    }

    if (!use_progress_box) {
        m_menu->RefreshViews();
        log_write("did delete\n");
    } else {
        // the thread and the callback both need to know which path failed.
        const auto failed_path = std::make_shared<fs::FsPath>();
        App::Push<ProgressBox>(0, "Deleting"_i18n, "", [this, failed_path](auto pbox) -> Result {
            FsDirCollections collections;
            auto& selected = m_menu->m_selected;
            auto src_fs = selected.m_view->GetFs();

            // build list of dirs / files
            for (const auto&p : selected.m_files) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto full_path = GetNewPath(selected.m_path, p.name);
                if (p.IsDir()) {
                    pbox->NewTransfer(i18n::Reorder("Scanning ", full_path));
                    R_TRY(get_collections(src_fs, full_path, p.name, collections));
                }
            }

            return DeleteAllCollectionsWithSelected(pbox, src_fs, selected, collections, FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles, failed_path.get());
        }, [this, failed_path](Result rc){
            std::string detail;
            if (std::strlen(*failed_path)) {
                // keep the end of long paths, as that is the part that identifies the entry.
                const std::string_view path{failed_path->s};
                constexpr size_t MAX_LEN = 90;
                detail = "Path: "_i18n + (path.size() > MAX_LEN ? "..." + std::string{path.substr(path.size() - MAX_LEN)} : std::string{path});
            }
            App::PushErrorBox(rc, "Failed to delete one or more files"_i18n, detail);

            m_menu->RefreshViews();
            log_write("did delete\n");
        });
    }
}

void FsView::OnPasteCallback() {
    // check if we only have 1 file / folder and is cut (rename)
    if (m_menu->m_selected.SameFs(this) && m_menu->m_selected.m_files.size() == 1 && m_menu->m_selected.m_type == SelectedType::Cut) {
        const auto& entry = m_menu->m_selected.m_files[0];
        const auto full_path = GetNewPath(m_menu->m_selected.m_path, entry.name);

        if (entry.IsDir()) {
            m_fs->RenameDirectory(full_path, GetNewPath(entry));
        } else {
            m_fs->RenameFile(full_path, GetNewPath(entry));
        }

        m_menu->RefreshViews();
    } else {
        App::Push<ProgressBox>(0, "Pasting"_i18n, "", [this](auto pbox) -> Result {
            auto& selected = m_menu->m_selected;
            auto src_fs = selected.m_view->GetFs();
            const auto is_same_fs = selected.SameFs(this);

            if (selected.SameFs(this) && selected.m_type == SelectedType::Cut) {
                for (const auto& p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto src_path = GetNewPath(selected.m_path, p.name);
                    const auto dst_path = GetNewPath(m_path, p.name);

                    pbox->SetTitle(p.name);
                    pbox->NewTransfer(i18n::Reorder("Pasting ", src_path));

                    if (p.IsDir()) {
                        m_fs->RenameDirectory(src_path, dst_path);
                    } else {
                        m_fs->RenameFile(src_path, dst_path);
                    }
                }
            } else {
                FsDirCollections collections;

                const auto on_paste_file = [&](auto& src_path, auto& dst_path) -> Result {
                    if (selected.m_type == SelectedType::Cut) {
                        // update timestamp if possible.
                        if (!m_fs->IsNative()) {
                            FsTimeStampRaw ts;
                            if (R_SUCCEEDED(src_fs->GetFileTimeStampRaw(src_path, &ts))) {
                                m_fs->SetTimestamp(dst_path, &ts);
                            }
                        }

                        // delete src file. folders are removed after.
                        R_TRY(src_fs->DeleteFile(src_path));
                    }

                    R_SUCCEED();
                };

                // build list of dirs / files
                for (const auto&p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto full_path = GetNewPath(selected.m_path, p.name);
                    if (p.IsDir()) {
                        pbox->NewTransfer(i18n::Reorder("Scanning ", full_path));
                        R_TRY(get_collections(src_fs, full_path, p.name, collections));
                    }
                }

                for (const auto& p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto src_path = GetNewPath(selected.m_path, p.name);
                    const auto dst_path = GetNewPath(p);

                    if (p.IsDir()) {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer(i18n::Reorder("Creating ", dst_path));
                        m_fs->CreateDirectory(dst_path);
                    } else {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer(i18n::Reorder("Copying ", src_path));
                        R_TRY(pbox->CopyFile(src_fs, m_fs.get(), src_path, dst_path, is_same_fs));
                        R_TRY(on_paste_file(src_path, dst_path));
                    }
                }

                // copy everything in collections
                for (const auto& c : collections) {
                    const auto base_dst_path = GetNewPath(m_path, c.parent_name);

                    for (const auto& p : c.dirs) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());

                        // const auto src_path = GetNewPath(c.path, p.name);
                        const auto dst_path = GetNewPath(base_dst_path, p.name);

                        pbox->SetTitle(p.name);
                        pbox->NewTransfer(i18n::Reorder("Creating ", dst_path));
                        m_fs->CreateDirectory(dst_path);
                    }

                    for (const auto& p : c.files) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());

                        const auto src_path = GetNewPath(c.path, p.name);
                        const auto dst_path = GetNewPath(base_dst_path, p.name);

                        pbox->SetTitle(p.name);
                        pbox->NewTransfer(i18n::Reorder("Copying ", src_path));
                        R_TRY(pbox->CopyFile(src_fs, m_fs.get(), src_path, dst_path, is_same_fs));
                        R_TRY(on_paste_file(src_path, dst_path));
                    }
                }

                // moving accross fs is not possible, thus files have to be copied.
                // this leaves the files on the src_fs.
                // the files are deleted one by one after a successfull copy (see above)
                // however this leaves the folders.
                // the folders cannot be deleted until the end as they have to be removed in
                // reverse order so that the folder can be deleted (it must be empty).
                if (selected.m_type == SelectedType::Cut) {
                    R_TRY(DeleteAllCollectionsWithSelected(pbox, src_fs, selected, collections, FsDirOpenMode_ReadDirs));
                }
            }

            R_SUCCEED();
        }, [this](Result rc){
            App::PushErrorBox(rc, "Failed to paste one or more files"_i18n);

            m_menu->RefreshViews();
            log_write("did paste\n");
        });
    }
}

void FsView::OnRenameCallback() {

}

auto FsView::get_collection(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    out.path = path;
    out.parent_name = parent_name;

    const auto fetch = [fs, &path](std::vector<FsDirectoryEntry>& out, u32 flags) -> Result {
        fs::Dir d;
        R_TRY(fs->OpenDirectory(path, flags, &d));
        return d.ReadAll(out);
    };

    if (inc_file) {
        u32 flags = FsDirOpenMode_ReadFiles;
        if (!inc_size) {
            flags |= FsDirOpenMode_NoFileSize;
        }
        R_TRY(fetch(out.files, flags));
    }

    if (inc_dir) {
        R_TRY(fetch(out.dirs, FsDirOpenMode_ReadDirs));
    }

    R_SUCCEED();
}

auto FsView::get_collections(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    // get a list of all the files / dirs
    FsDirCollection collection;
    R_TRY(get_collection(fs, path, parent_name, collection, true, true, inc_size));
    log_write("got collection: %s parent_name: %s files: %zu dirs: %zu\n", path.s, parent_name.s, collection.files.size(), collection.dirs.size());
    out.emplace_back(collection);

    // for (size_t i = 0; i < collection.dirs.size(); i++) {
    for (const auto&p : collection.dirs) {
        // use heap as to not explode the stack
        const auto new_path = std::make_unique<fs::FsPath>(FsView::GetNewPath(path, p.name));
        const auto new_parent_name = std::make_unique<fs::FsPath>(FsView::GetNewPath(parent_name, p.name));
        log_write("trying to get nested collection: %s parent_name: %s\n", new_path->s, new_parent_name->s);
        R_TRY(get_collections(fs, *new_path, *new_parent_name, out, inc_size));
    }

    R_SUCCEED();
}

auto FsView::get_collection(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    return get_collection(m_fs.get(), path, parent_name, out, inc_file, inc_dir, inc_size);
}

auto FsView::get_collections(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    return get_collections(m_fs.get(), path, parent_name, out, inc_size);
}

// remembers which path failed, so that the error box can show it.
static Result RememberFailedPath(Result rc, const fs::FsPath& path, fs::FsPath* failed_path) {
    if (R_FAILED(rc) && failed_path) {
        *failed_path = path;
    }
    return rc;
}

Result FsView::DeleteAllCollections(ProgressBox* pbox, fs::Fs* fs, const FsDirCollections& collections, u32 mode, fs::FsPath* failed_path) {
    // delete everything in collections, reversed
    for (const auto& c : std::views::reverse(collections)) {
        const auto delete_func = [&](auto& array) -> Result {
            for (const auto& p : array) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto full_path = FsView::GetNewPath(c.path, p.name);
                pbox->SetTitle(p.name);
                pbox->NewTransfer(i18n::Reorder("Deleting ", full_path.toString()));
                if ((mode & FsDirOpenMode_ReadDirs) && p.type == FsDirEntryType_Dir) {
                    log_write("deleting dir: %s\n", full_path.s);
                    R_TRY(RememberFailedPath(fs->DeleteDirectory(full_path), full_path, failed_path));
                    svcSleepThread(1e+5);
                } else if ((mode & FsDirOpenMode_ReadFiles) && p.type == FsDirEntryType_File) {
                    log_write("deleting file: %s\n", full_path.s);
                    R_TRY(RememberFailedPath(fs->DeleteFile(full_path), full_path, failed_path));
                    svcSleepThread(1e+5);
                }
            }

            R_SUCCEED();
        };

        R_TRY(delete_func(c.files));
        R_TRY(delete_func(c.dirs));
    }

    R_SUCCEED();
}

static Result DeleteAllCollectionsWithSelected(ProgressBox* pbox, fs::Fs* fs, const SelectedStash& selected, const FsDirCollections& collections, u32 mode = FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles, fs::FsPath* failed_path = nullptr) {
    R_TRY(FsView::DeleteAllCollections(pbox, fs, collections, mode, failed_path));

    for (const auto& p : selected.m_files) {
        pbox->Yield();
        R_TRY(pbox->ShouldExitResult());

        const auto full_path = FsView::GetNewPath(selected.m_path, p.name);
        pbox->SetTitle(p.name);
        pbox->NewTransfer(i18n::Reorder("Deleting ", full_path.toString()));

        if ((mode & FsDirOpenMode_ReadDirs) && p.type == FsDirEntryType_Dir) {
            log_write("deleting dir: %s\n", full_path.s);
            R_TRY(RememberFailedPath(fs->DeleteDirectory(full_path), full_path, failed_path));
        } else if ((mode & FsDirOpenMode_ReadFiles) && p.type == FsDirEntryType_File) {
            log_write("deleting file: %s\n", full_path.s);
            R_TRY(RememberFailedPath(fs->DeleteFile(full_path), full_path, failed_path));
        }
    }

    R_SUCCEED();
}

void FsView::SetFs(const std::shared_ptr<fs::Fs>& fs, const fs::FsPath& new_path, const FsEntry& new_entry) {
    if (m_fs && m_fs_entry.root == new_entry.root && m_fs_entry.type == new_entry.type) {
        log_write("same fs, ignoring\n");
        return;
    }

    if (m_loader) m_loader->Cancel();
    m_selection_key = std::to_string(static_cast<int>(new_entry.type)) + ":" + new_entry.root.toString();
    m_selection_revision = ~std::uint64_t{0};
    m_scanned = false;
    m_sort_pending = false;
    m_restore_after_load.reset();
    m_load_error.clear();
    m_path = new_path;
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();
    m_entries_current = {};
    m_previous_highlighted_file.clear();
    m_menu->m_selected.Reset();
    m_selected_count = 0;
    m_fs_entry = new_entry;
    m_fs = fs;

    if (HasFocus()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }
    }
}

void FsView::DisplayHash(hash::Type type) {
    // hack because we cannot share output between threaded calls...
    static std::string hash_out;
    hash_out.clear();

    App::Push<ProgressBox>(0, "Hashing"_i18n, GetEntry().name, [this, type](auto pbox) -> Result {
        const auto full_path = GetNewPathCurrent();
        pbox->NewTransfer(full_path);
        R_TRY(hash::Hash(pbox, type, m_fs.get(), full_path, hash_out));

        R_SUCCEED();
    }, [this, type](Result rc){
        App::PushErrorBox(rc, "Failed to hash file..."_i18n);

        if (R_SUCCEEDED(rc)) {
            char buf[0x100];
            // std::snprintf(buf, sizeof(buf), "%s\n%s\n%s", hash::GetTypeStr(type), hash_out.c_str(), GetEntry().GetName());
            std::snprintf(buf, sizeof(buf), "%s\n%s", hash::GetTypeStr(type), hash_out.c_str());
            App::Push<OptionBox>(buf, "OK"_i18n);
        }
    });
}

void FsView::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("File Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (m_menu->m_options & FsOption_CanSelect) {
        if (!m_entries_current.empty()) options->Add<SidebarEntryCallback>("Select all visible", [this] {
            SelectAllVisible();
        }, true);
        if (m_menu->m_cart->Count()) {
            options->Add<SidebarEntryCallback>("Clear selection", [this] {
                ClearSelection();
            }, true);
        }
    }

    SidebarEntryArray::Items mount_items;
    std::vector<FsEntry> fs_entries;

    for (const auto& e: FS_ENTRIES) {
        fs_entries.emplace_back(e);
        mount_items.push_back(i18n::get(e.name));
    }

    if (m_menu->m_custom_fs) {
        fs_entries.emplace_back(m_menu->m_custom_fs_entry);
        mount_items.push_back(m_menu->m_custom_fs_entry.name);
    }

    const auto stdio_locations = location::GetStdio(false);
    for (const auto& e: stdio_locations) {
        if (e.fs_hidden) {
            continue;
        }

        fs_entries.emplace_back(e.name, e.mount, FsType::Stdio, e.flags);
        mount_items.push_back(e.name);
    }

    options->Add<SidebarEntryArray>("Mount"_i18n, mount_items, [this, fs_entries](s64& index_out){
        App::PopToMenu();
        SetFs(m_menu->CreateFs(fs_entries[index_out]), fs_entries[index_out].root, fs_entries[index_out]);
    }, i18n::get(m_fs_entry.name));

    options->Add<SidebarEntryCallback>("Sort By"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("Sort Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        SidebarEntryArray::Items sort_items;
        sort_items.push_back("Size"_i18n);
        sort_items.push_back("Alphabetical"_i18n);

        SidebarEntryArray::Items order_items;
        order_items.push_back("Descending"_i18n);
        order_items.push_back("Ascending"_i18n);

        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            m_menu->m_sort.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_sort.Get());

        options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
            m_menu->m_order.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_order.Get());

        options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_menu->m_show_hidden.Get(), [this](bool& v_out){
            m_menu->m_show_hidden.Set(v_out);
            SortAndFindLastFile();
        });

        options->Add<SidebarEntryBool>("Folders First"_i18n, m_menu->m_folders_first.Get(), [this](bool& v_out){
            m_menu->m_folders_first.Set(v_out);
            SortAndFindLastFile();
        });

        options->Add<SidebarEntryBool>("Hidden Last"_i18n, m_menu->m_hidden_last.Get(), [this](bool& v_out){
            m_menu->m_hidden_last.Set(v_out);
            SortAndFindLastFile();
        });
    });

    if (m_entries_current.size()) {
        if (!m_fs_entry.IsReadOnly()) {
            options->Add<SidebarEntryCallback>("Cut"_i18n, [this](){
                m_menu->AddSelectedEntries(SelectedType::Cut);
            }, true);
        }

        options->Add<SidebarEntryCallback>("Copy"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Copy);
        }, true);
    }

    if (!m_menu->m_selected.Empty() && !m_fs_entry.IsReadOnly() && (m_menu->m_selected.Type() == SelectedType::Cut || m_menu->m_selected.Type() == SelectedType::Copy)) {
        options->Add<SidebarEntryCallback>("Paste"_i18n, [this](){
            const std::string buf = "Paste file(s)?"_i18n;
            App::Push<OptionBox>(
                buf, "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                if (op_index && *op_index) {
                    App::PopToMenu();
                    OnPasteCallback();
                }
            });
        });
    }

    // can't rename more than 1 file
    if (m_entries_current.size() && !m_selected_count && !m_fs_entry.IsReadOnly()) {
        options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            std::string out;
            const auto& entry = GetEntry();
            const auto name = entry.GetName();
            const auto header = "Set new name"_i18n;
            if (R_SUCCEEDED(swkbd::ShowText(out, header.c_str(), header.c_str(), name.c_str())) && !out.empty() && out != name) {
                App::PopToMenu();

                const auto src_path = GetNewPath(entry);
                const auto dst_path = GetNewPath(m_path, out);

                Result rc;
                if (entry.IsFile()) {
                    rc = m_fs->RenameFile(src_path, dst_path);
                } else {
                    rc = m_fs->RenameDirectory(src_path, dst_path);
                }

                if (R_SUCCEEDED(rc)) {
                    Scan(m_path);
                } else {
                    const auto msg = std::string("Failed to rename file: ") + entry.name;
                    App::PushErrorBox(rc, msg);
                }
            }
        });
    }

    if (m_entries_current.size() && !m_fs_entry.IsReadOnly()) {
        options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Delete);

            log_write("clicked on delete\n");
            App::Push<OptionBox>(
                "Delete Selected files?"_i18n, "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                    if (op_index && *op_index) {
                        App::PopToMenu();
                        OnDeleteCallback();
                    }
                }
            );
            log_write("pushed delete\n");
        });
    }

    // returns true if all entries match the ext array.
    const auto check_all_ext = [this](const auto& exts){
        return AllSelectedEntries([&exts](const auto& entry) {
            return IsExtension(entry.GetExtension(), exts);
        });
    };

    if (m_menu->CanInstall()) {
        if (m_entries_current.size() || m_menu->m_cart->Count()) {
            if (m_menu->m_cart->Count() || AllSelectedEntries([](const auto& entry) {
                return entry.IsDir() || IsExtension(entry.GetExtension(), INSTALL_EXTENSIONS);
            })) {
                auto entry = options->Add<SidebarEntryCallback>("Scan and install selection", [this](){
                    InstallFiles();
                });
                entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
            }
        }

    }

    if (m_entries_current.size()) {
        if (check_all_ext(ZIP_EXTENSIONS) && !m_fs_entry.IsReadOnly()) {
            options->Add<SidebarEntryCallback>("Extract zip"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Extract Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Extract here"_i18n, [this](){
                    UnzipFiles("");
                });

                options->Add<SidebarEntryCallback>("Extract to root"_i18n, [this](){
                    App::Push<OptionBox>("Are you sure you want to extract to root?"_i18n,
                        "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                        if (op_index && *op_index) {
                            UnzipFiles(m_fs->Root());
                        }
                    });
                });

                options->Add<SidebarEntryCallback>("Extract to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Extract path", "Enter the path to the folder to extract into"_i18n.c_str(), fs::AppendPath(m_path, ""))) && !out.empty()) {
                        UnzipFiles(out);
                    }
                });
            });
        }

        if ((!check_all_ext(ZIP_EXTENSIONS) || m_selected_count) && !m_fs_entry.IsReadOnly()) {
            options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Compress Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Compress"_i18n, [this](){
                    ZipFiles("");
                });

                options->Add<SidebarEntryCallback>("Compress to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Compress path", "Enter the path to the folder to compress into"_i18n.c_str(), m_path)) && !out.empty()) {
                        ZipFiles(out);
                    }
                });
            });
        }
    }

    options->Add<SidebarEntryCallback>("Advanced"_i18n, [this](){
        DisplayAdvancedOptions();
    });
}

void FsView::DisplayAdvancedOptions() {
    auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (!m_fs_entry.IsReadOnly()) {
        options->Add<SidebarEntryCallback>("Create File"_i18n, [this](){
            std::string out;
            const auto header = "Set File Name"_i18n;
            if (R_SUCCEEDED(swkbd::ShowText(out, header.c_str(), header.c_str(), fs::AppendPath(m_path, ""))) && !out.empty()) {
                App::PopToMenu();

                fs::FsPath full_path;
                if (out.starts_with(m_fs_entry.root.s)) {
                    full_path = out;
                } else {
                    full_path = fs::AppendPath(m_path, out);
                }

                m_fs->CreateDirectoryRecursivelyWithPath(full_path);
                if (R_SUCCEEDED(m_fs->CreateFile(full_path, 0, 0))) {
                    log_write("created file: %s\n", full_path.s);
                    Scan(m_path);
                } else {
                    log_write("failed to create file: %s\n", full_path.s);
                }
            }
        });

        options->Add<SidebarEntryCallback>("Create Folder"_i18n, [this](){
            std::string out;
            const auto header = "Set Folder Name"_i18n;
            if (R_SUCCEEDED(swkbd::ShowText(out, header.c_str(), header.c_str(), fs::AppendPath(m_path, ""))) && !out.empty()) {
                App::PopToMenu();

                fs::FsPath full_path;
                if (out.starts_with(m_fs_entry.root.s)) {
                    full_path = out;
                } else {
                    full_path = fs::AppendPath(m_path, out);
                }

                if (R_SUCCEEDED(m_fs->CreateDirectoryRecursively(full_path))) {
                    log_write("created dir: %s\n", full_path.s);
                    Scan(m_path);
                } else {
                    log_write("failed to create dir: %s\n", full_path.s);
                }
            }
        });
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile() && GetEntry().file_size >= 0 && GetEntry().file_size < 1024*64) {
        options->Add<SidebarEntryCallback>("Edit as text"_i18n, [this](){
            App::Push<fileview::Menu>(GetFs(), GetNewPathCurrent());
        });
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        options->Add<SidebarEntryCallback>("Hash"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Hash Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryCallback>("CRC32"_i18n, [this](){
                DisplayHash(hash::Type::Crc32);
            });
            options->Add<SidebarEntryCallback>("MD5"_i18n, [this](){
                DisplayHash(hash::Type::Md5);
            });
            options->Add<SidebarEntryCallback>("SHA1"_i18n, [this](){
                DisplayHash(hash::Type::Sha1);
            });
            options->Add<SidebarEntryCallback>("SHA256"_i18n, [this](){
                DisplayHash(hash::Type::Sha256);
            });
            options->Add<SidebarEntryCallback>("/dev/null (Speed Test)"_i18n, [this](){
                DisplayHash(hash::Type::Null);
            });
        });
    }

    options->Add<SidebarEntryBool>("Ignore read only"_i18n, m_menu->m_ignore_read_only.Get(), [this](bool& v_out){
        m_menu->m_ignore_read_only.Set(v_out);
        m_fs->SetIgnoreReadOnly(v_out);
    });
}

Base::Base(u32 flags, u32 options)
: MenuBase{"FileBrowser"_i18n, flags}
, m_options{options} {
    Init(CreateFs(FS_ENTRY_DEFAULT), FS_ENTRY_DEFAULT, {}, false);
}

Base::Base(const std::shared_ptr<fs::Fs>& fs, const FsEntry& fs_entry, const fs::FsPath& path, bool is_custom, u32 flags, u32 options)
: MenuBase{"FileBrowser"_i18n, flags}
, m_options{options} {
    Init(fs, fs_entry, path, is_custom);
}

void Base::Update(Controller* controller, TouchInfo* touch) {
    nxloader_ui::SelectionSizer::PollRetired();
    m_cart->Resume();
    m_cart->Poll();
    if (m_review_after_selection && !m_cart->Queued()) {
        m_review_after_selection = false;
        nxloader_ui::ScanAndInstall(m_cart);
        return;
    }
    if (IsSplitScreen()) {
        view_left->PollLoad();
        view_right->PollLoad();
    } else {
        view->PollLoad();
    }
    if (IsSplitScreen()) {
        view_left->PollSizes();
        view_right->PollSizes();
    } else {
        view->PollSizes();
    }
    UpdateSubheading();
    if (g_change_signalled.exchange(false)) {

        if (IsSplitScreen()) {
            view_left->SortAndFindLastFile(true);
            view_right->SortAndFindLastFile(true);
        } else {
            view->SortAndFindLastFile(true);
        }
    }

    // workaround the buttons not being display properly.
    // basically, inherit all actions from the view, draw them,
    // then restore state after.
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Update(controller, touch);
    view->Update(controller, touch);
}

void Base::Draw(NVGcontext* vg, Theme* theme) {
    // see Base::Update().
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Draw(vg, theme);

    if (IsSplitScreen()) {
        view_left->Draw(vg, theme);
        view_right->Draw(vg, theme);

        if (view == view_left.get()) {
            gfx::drawRect(vg, view_right->GetPos(), theme->GetColour(ThemeEntryID_FOCUS), 5);
        } else {
            gfx::drawRect(vg, view_left->GetPos(), theme->GetColour(ThemeEntryID_FOCUS), 5);
        }

        gfx::drawRect(vg, SCREEN_WIDTH/2, GetY(), 1, GetH(), theme->GetColour(ThemeEntryID_LINE));
    } else {
        view->Draw(vg, theme);
    }
}

void Base::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (IsSplitScreen()) {
        view_left->OnFocusGained();
        view_right->OnFocusGained();
    } else {
        view->OnFocusGained();
    }
}

auto Base::PauseSizeEstimates() -> std::vector<std::shared_ptr<nxloader_ui::SelectionSizer>> {
    return m_cart->Pause();
}

void Base::OnFocusLost() {
    MenuBase::OnFocusLost();
    PauseSizeEstimates();
}

void Base::OnClick(FsView* view, const FsEntry& fs_entry, const FileEntry& entry, const fs::FsPath& path) {
    view->OnClick();
}

void Base::UpdateSubheading() {
    const auto index = view->m_entries_current.empty() ? 0 : view->m_index + 1;
    auto heading = std::to_string(index) + " / " + std::to_string(view->m_entries_current.size());
    const auto total = m_cart->Totals();
    if (m_options & FsOption_CanSelect) {
        heading += " | " + std::to_string(m_cart->Count()) + " selected across folders";
        if (m_cart->Pending()) heading += " | Sizing " + std::to_string(m_cart->Pending());
        if (m_cart->Count() && App::GetApp()->m_selection_details.Get()) {
            heading += " | Files " + utils::formatSizeStorage(total.source_bytes);
            heading += " | Install " + std::string(total.unknown_count ? ">=" : "~") + utils::formatSizeStorage(total.installed_bytes);
            if (total.unknown_count) heading += " + unknown";
        }
    }
    SetStoragePreview(m_cart->Count() ? std::optional<StoragePreview>{{total, App::GetInstallSdEnable(), m_cart->Pending()}} : std::nullopt);
    SetSubHeading(heading);
}

void Base::SetSplitScreen(bool enable) {
    if (!(m_options & FsOption_CanSplit)) {
        return;
    }

    if (m_split_screen != enable) {
        m_split_screen = enable;

        if (m_split_screen) {
            const auto change_view = [this](FsView* new_view){
                if (view != new_view) {
                    view->OnFocusLost();
                    view = new_view;
                    view->OnFocusGained();
                    SetTitleSubHeading(view->m_path);
                    UpdateSubheading();
                }
            };

            // load second screen as a copy of the left side.
            view->SetSide(ViewSide::Left);
            view_right = std::make_unique<FsView>(view, ViewSide::Right);
            change_view(view_right.get());

            SetAction(Button::LEFT, Action{[this, change_view](){
                change_view(view_left.get());
            }});
            SetAction(Button::RIGHT, Action{[this, change_view](){
                change_view(view_right.get());
            }});
        } else {
            if (view == view_right.get()) {
                view_left = std::move(view_right);
            }

            view_right = {};
            view = view_left.get();
            view->SetSide(ViewSide::Left);

            RemoveAction(Button::LEFT);
            RemoveAction(Button::RIGHT);
            ResetSelection();
        }
    }
}

void Base::RefreshViews() {
    ResetSelection();

    if (IsSplitScreen()) {
        view_left->Scan(view_left->m_path);
        view_right->Scan(view_right->m_path);
    } else {
        view->Scan(view->m_path);
    }
}

void Base::PromptIfShouldExit() {
    if (IsTab()) {
        return;
    }

    if (m_options & FsOption_DoNotPrompt) {
        SetPop();
        return;
    }

    App::Push<ui::OptionBox>(
        "Close FileBrowser?"_i18n,
        "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

auto Base::CreateFs(const FsEntry& fs_entry) -> std::shared_ptr<fs::Fs> {
    switch (fs_entry.type) {
        case FsType::Sd:
            return std::make_shared<fs::FsNativeSd>(m_ignore_read_only.Get());
        case FsType::Stdio:
            return std::make_shared<fs::FsStdio>(true, fs_entry.root);
        case FsType::Custom:
            return m_custom_fs;
    }

    std::unreachable();
}

void Base::Init(const std::shared_ptr<fs::Fs>& fs, const FsEntry& fs_entry, const fs::FsPath& path, bool is_custom) {
    if (m_options & FsOption_CanSplit) {
        SetAction(Button::L3, Action{"Split"_i18n, [this](){
            SetSplitScreen(IsSplitScreen() ^ 1);
        }});
    }

    if (!IsTab()) {
        SetAction(Button::SELECT, Action{"Close"_i18n, [this](){
            PromptIfShouldExit();
        }});
    }

    if (is_custom) {
        m_custom_fs = fs;
        m_custom_fs_entry = fs_entry;
    }

    log_write("creating view\n");

    view_left = std::make_unique<FsView>(this, fs, path, fs_entry, ViewSide::Left);
    view = view_left.get();
}

} // namespace sphaira::ui::menu::filebrowser
