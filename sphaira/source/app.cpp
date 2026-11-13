#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "ui/menus/main_menu.hpp"

#include "app.hpp"
#include "nxloader/selection_size.hpp"
#include "yati/yati.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "nro.hpp"
#include "nacp_compat.hpp"
#include "evman.hpp"
#include "image.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"

#include "ui/menus/file_viewer.hpp"
#include "utils/profile.hpp"
#include "utils/thread.hpp"

#include <nanovg_dk.h>
#include <minIni.h>
#include <algorithm>
#include <ranges>
#include <cassert>
#include <cstring>
#include <ctime>
#include <span>
#include <dirent.h>
#include <sys/stat.h>

#ifdef ENABLE_LIBUSBHSFS
    #include <usbhsfs.h>
#endif // ENABLE_LIBUSBHSFS

extern "C" {
    u32 __nx_applet_exit_mode = 0;
} // extern "C"

namespace sphaira {

#ifdef ENABLE_LIBUSBHSFS
namespace {

// builds the libusbhsfs mount flags out of the user's hdd options and pushes
// them to the library. only affects devices mounted *after* this call, use
// App::RemountHdd() if the change should be visible right away.
//
// most of these are NTFS only, they are ignored for FAT/exFAT volumes.
void ApplyHddMountFlags() {
    u32 flags = UsbHsFsMountFlags_None;

    if (App::GetWriteProtect()) {
        flags |= UsbHsFsMountFlags_ReadOnly;
    } else {
        // an NTFS volume with a dirty journal (the normal state after the drive
        // was pulled out of a pc without ejecting it) is refused for writing
        // unless the journal is replayed first.
        flags |= UsbHsFsMountFlags_ReplayJournal;
    }

    // windows marks plenty of ordinary folders as hidden, without this they
    // would silently not show up in the file browser.
    flags |= UsbHsFsMountFlags_ShowHiddenFiles;

    // deliberately *not* setting UsbHsFsMountFlags_UpdateAccessTimes (which is
    // part of the library default), writing back an access time after every
    // read costs a lot of speed when installing from an NTFS drive.

    if (App::GetHddShowSystemFiles()) {
        flags |= UsbHsFsMountFlags_ShowSystemFiles;
    }

    if (App::GetHddIgnoreHibernation()) {
        flags |= UsbHsFsMountFlags_IgnoreHibernation;
    }

    usbHsFsSetFileSystemMountFlags(flags);
}

} // namespace
#endif // ENABLE_LIBUSBHSFS

namespace {

constexpr const u8 DEFAULT_IMAGE_DATA[]{
    #embed <icons/default.png>
};

struct ThemeData {
    std::string elements[ThemeEntryID_MAX]{};
};

struct ThemeIdPair {
    const char* label;
    ThemeEntryID id;
    ElementType type{ElementType::None};
};

struct FrameBufferSize {
    Vec2 size;
    Vec2 scale;
};

constexpr KeyboardState::MapEntry KEYBOARD_BUTTON_MAP[] = {
    {HidKeyboardKey_UpArrow,        static_cast<u64>(Button::DPAD_UP)},
    {HidKeyboardKey_DownArrow,      static_cast<u64>(Button::DPAD_DOWN)},
    {HidKeyboardKey_LeftArrow,      static_cast<u64>(Button::DPAD_LEFT)},
    {HidKeyboardKey_RightArrow,     static_cast<u64>(Button::DPAD_RIGHT)},

    {HidKeyboardKey_W,              static_cast<u64>(Button::DPAD_UP)},
    {HidKeyboardKey_S,              static_cast<u64>(Button::DPAD_DOWN)},
    {HidKeyboardKey_A,              static_cast<u64>(Button::DPAD_LEFT)},
    {HidKeyboardKey_D,              static_cast<u64>(Button::DPAD_RIGHT)},

    // options (may swap).
    {HidKeyboardKey_Z,              static_cast<u64>(Button::Y)},
    {HidKeyboardKey_X,              static_cast<u64>(Button::X)},

    // menus.
    {HidKeyboardKey_Q,              static_cast<u64>(Button::L)},
    {HidKeyboardKey_E,              static_cast<u64>(Button::R)},

    // select and back.
    {HidKeyboardKey_Return,         static_cast<u64>(Button::A)},
    {HidKeyboardKey_Space,          static_cast<u64>(Button::A)},
    {HidKeyboardKey_Backspace,      static_cast<u64>(Button::B)},

    // exit.
    {HidKeyboardKey_Escape,         static_cast<u64>(Button::START)},

    // idk what this should map to.
    {HidKeyboardKey_R,              static_cast<u64>(Button::SELECT)},
};

constexpr ThemeIdPair THEME_ENTRIES[] = {
    { "background", ThemeEntryID_BACKGROUND },
    { "grid", ThemeEntryID_GRID },
    { "text", ThemeEntryID_TEXT, ElementType::Colour },
    { "text_info", ThemeEntryID_TEXT_INFO, ElementType::Colour },
    { "text_selected", ThemeEntryID_TEXT_SELECTED, ElementType::Colour },
    { "selected_background", ThemeEntryID_SELECTED_BACKGROUND, ElementType::Colour },
    { "error", ThemeEntryID_ERROR, ElementType::Colour },
    { "popup", ThemeEntryID_POPUP, ElementType::Colour },
    { "focus", ThemeEntryID_FOCUS, ElementType::Colour },
    { "line", ThemeEntryID_LINE, ElementType::Colour },
    { "line_separator", ThemeEntryID_LINE_SEPARATOR, ElementType::Colour },
    { "sidebar", ThemeEntryID_SIDEBAR, ElementType::Colour },
    { "scrollbar", ThemeEntryID_SCROLLBAR, ElementType::Colour },
    { "scrollbar_background", ThemeEntryID_SCROLLBAR_BACKGROUND, ElementType::Colour },
    { "progressbar", ThemeEntryID_PROGRESSBAR, ElementType::Colour },
    { "progressbar_background", ThemeEntryID_PROGRESSBAR_BACKGROUND, ElementType::Colour },
    { "highlight_1", ThemeEntryID_HIGHLIGHT_1, ElementType::Colour },
    { "highlight_2", ThemeEntryID_HIGHLIGHT_2, ElementType::Colour },
    { "icon_colour", ThemeEntryID_ICON_COLOUR, ElementType::Colour },
    { "icon_audio", ThemeEntryID_ICON_AUDIO, ElementType::Texture },
    { "icon_video", ThemeEntryID_ICON_VIDEO, ElementType::Texture },
    { "icon_image", ThemeEntryID_ICON_IMAGE, ElementType::Texture },
    { "icon_file", ThemeEntryID_ICON_FILE, ElementType::Texture },
    { "icon_folder", ThemeEntryID_ICON_FOLDER, ElementType::Texture },
    { "icon_zip", ThemeEntryID_ICON_ZIP, ElementType::Texture },
    { "icon_nro", ThemeEntryID_ICON_NRO, ElementType::Texture },
};

constinit App* g_app{};
Mutex g_screen_toggle_mutex{};
u32 g_screen_toggle_ref_count{};
bool g_screen_disabled{};

void deko3d_error_cb(void* userData, const char* context, DkResult result, const char* message) {
    switch (result) {
        case DkResult_Success:
            break;

        case DkResult_Fail:
            log_write("[DkResult_Fail] %s\n", message);
            App::Notify("DkResult_Fail");
            break;

        case DkResult_Timeout:
            log_write("[DkResult_Timeout] %s\n", message);
            App::Notify("DkResult_Timeout");
            break;

        case DkResult_OutOfMemory:
            log_write("[DkResult_OutOfMemory] %s\n", message);
            App::Notify("DkResult_OutOfMemory");
            break;

        case DkResult_NotImplemented:
            log_write("[DkResult_NotImplemented] %s\n", message);
            App::Notify("DkResult_NotImplemented");
            break;

        case DkResult_MisalignedSize:
            log_write("[DkResult_MisalignedSize] %s\n", message);
            App::Notify("DkResult_MisalignedSize");
            break;

        case DkResult_MisalignedData:
            log_write("[DkResult_MisalignedData] %s\n", message);
            App::Notify("DkResult_MisalignedData");
            break;

        case DkResult_BadInput:
            log_write("[DkResult_BadInput] %s\n", message);
            App::Notify("DkResult_BadInput");
            break;

        case DkResult_BadFlags:
            log_write("[DkResult_BadFlags] %s\n", message);
            App::Notify("DkResult_BadFlags");
            break;

        case DkResult_BadState:
            log_write("[DkResult_BadState] %s\n", message);
            App::Notify("DkResult_BadState");
            break;
    }
}

void on_applet_focus_state(App* app) {
    switch (appletGetFocusState()) {
        case AppletFocusState_InFocus:
            log_write("[APPLET] AppletFocusState_InFocus\n");
            // App::Notify("AppletFocusState_InFocus");
            break;

        case AppletFocusState_OutOfFocus:
            log_write("[APPLET] AppletFocusState_OutOfFocus\n");
            // App::Notify("AppletFocusState_OutOfFocus");
            break;

        case AppletFocusState_Background:
            log_write("[APPLET] AppletFocusState_Background\n");
            // App::Notify("AppletFocusState_Background");
            break;
    }
}

void on_applet_operation_mode(App* app) {
    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            log_write("[APPLET] AppletOperationMode_Handheld\n");
            App::Notify("Switch-Handheld!"_i18n);
            break;

        case AppletOperationMode_Console:
            log_write("[APPLET] AppletOperationMode_Console\n");
            App::Notify("Switch-Docked!"_i18n);
            break;
    }
}

void applet_on_performance_mode(App* app) {
    switch (appletGetPerformanceMode()) {
        case ApmPerformanceMode_Invalid:
            log_write("[APPLET] ApmPerformanceMode_Invalid\n");
            App::Notify("ApmPerformanceMode_Invalid");
            break;

        case ApmPerformanceMode_Normal:
            log_write("[APPLET] ApmPerformanceMode_Normal\n");
            App::Notify("ApmPerformanceMode_Normal");
            break;

        case ApmPerformanceMode_Boost:
            log_write("[APPLET] ApmPerformanceMode_Boost\n");
            App::Notify("ApmPerformanceMode_Boost");
            break;
    }
}

void appplet_hook_calback(AppletHookType type, void *param) {
    auto app = static_cast<App*>(param);
    switch (type) {
        case AppletHookType_OnFocusState:
            log_write("[APPLET] AppletHookType_OnFocusState\n");
            // App::Notify("AppletHookType_OnFocusState");
            on_applet_focus_state(app);
            break;

        case AppletHookType_OnOperationMode:
            log_write("[APPLET] AppletHookType_OnOperationMode\n");
            // App::Notify("AppletHookType_OnOperationMode");
            on_applet_operation_mode(app);
            break;

        case AppletHookType_OnPerformanceMode:
            log_write("[APPLET] AppletHookType_OnPerformanceMode\n");
            // App::Notify("AppletHookType_OnPerformanceMode");
            applet_on_performance_mode(app);
            break;

        case AppletHookType_OnExitRequest:
            log_write("[APPLET] AppletHookType_OnExitRequest\n");
            // App::Notify("AppletHookType_OnExitRequest");
            break;

        case AppletHookType_OnResume:
            log_write("[APPLET] AppletHookType_OnResume\n");
            // App::Notify("AppletHookType_OnResume");
            break;

        case AppletHookType_OnCaptureButtonShortPressed:
            log_write("[APPLET] AppletHookType_OnCaptureButtonShortPressed\n");
            // App::Notify("AppletHookType_OnCaptureButtonShortPressed");
            break;

        case AppletHookType_OnAlbumScreenShotTaken:
            log_write("[APPLET] AppletHookType_OnAlbumScreenShotTaken\n");
            break;

        case AppletHookType_RequestToDisplay:
            log_write("[APPLET] AppletHookType_RequestToDisplay\n");
            // App::Notify("AppletHookType_RequestToDisplay");
            break;

        case AppletHookType_Max:
            assert(!"AppletHookType_Max hit");
            break;
    }
}

auto GetFrameBufferSize() -> FrameBufferSize {
    FrameBufferSize fb{};

    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            fb.size.x = 1280;
            fb.size.y = 720;
            break;

        case AppletOperationMode_Console:
            fb.size.x = 1920;
            fb.size.y = 1080;
            break;
    }

    fb.scale.x = fb.size.x / SCREEN_WIDTH;
    fb.scale.y = fb.size.y / SCREEN_HEIGHT;
    return fb;
}

auto LoadThemeMeta(const fs::FsPath& path, ThemeMeta& meta) -> bool {
    meta = {};

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto meta = static_cast<ThemeMeta*>(UserData);

        if (!std::strcmp(Section, "meta")) {
            if (!std::strcmp(Key, "name")) {
                meta->name = Value;
            } else if (!std::strcmp(Key, "author")) {
                meta->author = Value;
            } else if (!std::strcmp(Key, "version")) {
                meta->version = Value;
            } else if (!std::strcmp(Key, "inherit")) {
                meta->inherit = Value;
            }

            return 1;
        }

        return 0;
    };

    if (!ini_browse(cb, &meta, path)) {
        return false;
    }

    if (meta.name.empty() || meta.author.empty() || meta.version.empty()) {
        return false;
    }

    log_write("loaded meta from: %s\n", path.s);
    meta.ini_path = path;
    return true;
}

void LoadThemeInternal(ThemeMeta meta, ThemeData& theme_data, int inherit_level = 0) {
    constexpr auto inherit_level_max = 5;

    // all themes will inherit from black theme by default.
    if (meta.inherit.empty() && !inherit_level) {
        meta.inherit = "romfs:/themes/base_black_theme.ini";
    }

    // check if the theme inherits from another, if so, load it.
    // block inheriting from itself.
    if (inherit_level < inherit_level_max && !meta.inherit.empty() && strcasecmp(meta.inherit, "none") && meta.inherit != meta.ini_path) {
        log_write("inherit is not empty: %s\n", meta.inherit.s);
        if (R_SUCCEEDED(romfsInit())) {
            ThemeMeta inherit_meta;
            const auto has_meta = LoadThemeMeta(meta.inherit, inherit_meta);
            romfsExit();

            // base themes do not have a meta
            if (!has_meta) {
                inherit_meta.ini_path = meta.inherit;
            }

            LoadThemeInternal(inherit_meta, theme_data, inherit_level + 1);
        }
    }

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto theme_data = static_cast<ThemeData*>(UserData);

        if (!std::strcmp(Section, "theme")) {
            for (auto& e : THEME_ENTRIES) {
                if (!std::strcmp(Key, e.label)) {
                    theme_data->elements[e.id] = Value;
                    break;
                }
            }
        }

        return 1;
    };

    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());

        if (!ini_browse(cb, &theme_data, meta.ini_path)) {
            log_write("failed to open ini: %s\n", meta.ini_path.s);
        } else {
            log_write("opened ini: %s\n", meta.ini_path.s);
        }
    }
}

void on_i18n_change() {
    i18n::exit();
    i18n::init(App::GetLanguage());
}

// The app was NXLoader-GPT before it was renamed, with its settings in
// /config/nxloader. Copies them once, so a renamed build keeps them.
void MigrateOldConfig() {
    constexpr const char* old_path = "/config/nxloader/config.ini";
    struct stat st;
    if (!stat(App::CONFIG_PATH, &st) || stat(old_path, &st)) {
        return;
    }

    auto in = std::fopen(old_path, "rb");
    if (!in) {
        return;
    }
    std::vector<char> data(st.st_size);
    const auto read = std::fread(data.data(), 1, data.size(), in);
    std::fclose(in);

    mkdir("/config", 0777);
    mkdir(APP_CONFIG_DIR, 0777);
    if (auto out = std::fopen(App::CONFIG_PATH, "wb")) {
        std::fwrite(data.data(), 1, read, out);
        std::fclose(out);
    }
}

// The first lines of every log, so each log says what produced it.
void LogSessionHeader() {
    const auto now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char started[32]{};
    std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &tm);

    log_write("==== " APP_NAME " " APP_DISPLAY_VERSION " ====\n");
    log_write("[session] started: %s\n", started);
    log_write("[session] path: %s\n", App::GetExePath().s);
    log_write("[session] mode: %s\n", App::IsApplication() ? "application" : "applet (reduced memory)");

    u64 total{}, used{};
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    log_write("[session] memory: %lu MiB used of %lu MiB\n", used >> 20, total >> 20);

    s64 free{}, size{};
    if (R_SUCCEEDED(App::GetSdSize(&free, &size))) {
        log_write("[session] microSD: %ld MiB free of %ld MiB\n", free >> 20, size >> 20);
    }
    if (R_SUCCEEDED(App::GetEmmcSize(&free, &size))) {
        log_write("[session] system storage: %ld MiB free of %ld MiB\n", free >> 20, size >> 20);
    }
    log_write("[session] updates: %s\n", APP_UPDATE_REPO[0] ? "github.com/" APP_UPDATE_REPO : "not configured");
}

} // namespace

void App::Loop() {
    // adjust these if FPSlocker ever supports different min/max fps.
    constexpr double min_delta    = 1000.0 / 120.0; // 120 fps
    constexpr double max_delta    = 1000.0 / 15.0;  // 15  fps
    constexpr double target_delta = 1000.0 / 60.0;  // 60  fps

    u64 start = armTicksToNs(armGetSystemTick());
    m_delta_time = 1.0;

    while (!m_quit && appletMainLoop()) {
        if (m_widgets.empty()) {
            m_quit = true;
            break;
        }

        ui::gfx::updateHighlightAnimation();

        // fire all events in in a 3ms timeslice
        TimeStamp ts_event;
        const u64 event_timeout = 3;

        // limit events to a max per frame in order to not block for too long.
        while (true) {
            if (ts_event.GetMs() >= event_timeout) {
                log_write("event loop timed-out\n");
                break;
            }

            auto event = evman::pop();
            if (!event.has_value()) {
                break;
            }

            std::visit([this](auto&& arg){
                using T = std::decay_t<decltype(arg)>;
                if constexpr(std::is_same_v<T, evman::LaunchNroEventData>) {
                    log_write("[LaunchNroEventData] got event\n");
                    // force disable pop-back to main menu.
                    __nx_applet_exit_mode = 0;
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, evman::ExitEventData>) {
                    log_write("[ExitEventData] got event\n");
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, curl::DownloadEventData>) {
                    log_write("[DownloadEventData] got event\n");
                    if (arg.callback && !arg.stoken.stop_requested()) {
                        arg.callback(arg.result);
                    }
                } else if constexpr(std::is_same_v<T, evman::CallbackEventData>) {
                    log_write("[CallbackEventData] got event\n");
                    if (arg.callback && !arg.stoken.stop_requested()) {
                        arg.callback();
                    }
                } else {
                    static_assert(false, "non-exhaustive visitor!");
                }
            }, event.value());
        }

        const auto fb = GetFrameBufferSize();
        if (fb.size.x != s_width || fb.size.y != s_height) {
            s_width = fb.size.x;
            s_height = fb.size.y;
            m_scale = fb.scale;
            this->destroyFramebufferResources();
            this->createFramebufferResources();
            renderer->UpdateViewSize(s_width, s_height);
        }

        this->Poll();
        this->Update();
        // with the backlight off nothing can be seen, so drawing only costs
        // power. docked, the tv still shows the picture.
        if (App::IsScreenOff() && appletGetOperationMode() == AppletOperationMode_Handheld) {
            svcSleepThread(50'000'000);
        } else {
            this->Draw();
        }

        // check how long this frame took.
        const u64 now = armTicksToNs(armGetSystemTick());
        // convert to ns.
        const double delta = (double)(now - start) / 1e+6;
        // clamp and normalise to 1.0 as the target, higher values if we took too long.
        m_delta_time = std::clamp(delta, min_delta, max_delta) / target_delta;
        // save timestamp for next frame.
        start = now;
    }
}

auto App::Push(std::unique_ptr<ui::Widget>&& widget) -> void {
    log_write("[APP] pushing widget\n");

    // when freeing widges, this may cancel a transfer which causes it to push
    // an error box, so check if we are quitting first before adding.
    if (g_app->m_quit) {
        log_write("[APP] is quitting, not pushing widget\n");
        return;
    }

    // check if the widget wants to pop before adding.
    // this can happen if something failed in the constructor and the widget wants to exit.
    if (widget->ShouldPop()) {
        return;
    }

    if (!g_app->m_widgets.empty()) {
        g_app->m_widgets.back()->OnFocusLost();
    }

    log_write("doing focus gained\n");
    g_app->m_widgets.emplace_back(std::forward<decltype(widget)>(widget))->OnFocusGained();
    log_write("did it\n");
}

auto App::Detach(const ui::Widget* widget) -> std::unique_ptr<ui::Widget> {
    auto& widgets = g_app->m_widgets;
    auto it = std::ranges::find_if(widgets, [widget](const auto& e) {
        return e.get() == widget;
    });

    if (it == widgets.end()) {
        return {};
    }

    const auto was_back = std::next(it) == widgets.end();
    auto out = std::move(*it);
    widgets.erase(it);
    out->OnFocusLost();

    if (was_back && !widgets.empty()) {
        widgets.back()->OnFocusGained();
    }

    return out;
}

auto App::PopToMenu() -> void {
    for (auto& p : std::ranges::views::reverse(g_app->m_widgets)) {
        if (p->IsMenu()) {
            break;
        }

        p->SetPop();
    }
}

void App::Notify(const std::string& text, ui::NotifEntry::Side side) {
    log_write("[NOTIFY] %s\n", text.c_str());
    g_app->m_notif_manager.Push({text, side});
}

void App::Notify(ui::NotifEntry entry) {
    g_app->m_notif_manager.Push(entry);
}

void App::NotifyPop(ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Pop(side);
}

void App::NotifyClear(ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Clear(side);
}

void App::NotifyFlashLed() {
    static constexpr HidsysNotificationLedPattern pattern = {
        .baseMiniCycleDuration = 0x1,             // 12.5ms.
        .totalMiniCycles = 0x1,                   // 1 mini cycle(s).
        .totalFullCycles = 0x1,                   // 1 full run(s).
        .startIntensity = 0xF,                    // 100%.
        .miniCycles = {{
            .ledIntensity = 0xF,                  // 100%.
            .transitionSteps = 0xF,               // 1 step(s). Total 12.5ms.
            .finalStepDuration = 0xF,             // Forced 12.5ms.
        }}
    };

    Result rc;
    s32 total;
    HidsysUniquePadId unique_pad_id;

    rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_Handheld, &unique_pad_id, 1, &total);
    if (R_SUCCEEDED(rc) && total) {
        rc = hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
    }

    if (R_FAILED(rc) || !total) {
        rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, &unique_pad_id, 1, &total);
        if (R_SUCCEEDED(rc) && total) {
            hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
        }
    }
}

Result App::PushErrorBox(Result rc, const std::string& message) {
    if (R_FAILED(rc)) {
        App::Push<ui::ErrorBox>(rc, message);
    }
    return rc;
}

Result App::PushErrorBox(Result rc, const std::string& message, const std::string& detail) {
    if (R_FAILED(rc)) {
        App::Push<ui::ErrorBox>(rc, message, detail);
    }
    return rc;
}

auto App::GetDefaultImage() -> int {
    return g_app->m_default_image;
}

auto App::GetDefaultImageData() -> std::span<const u8> {
    return DEFAULT_IMAGE_DATA;
}

auto App::GetExePath() -> fs::FsPath {
    return g_app->m_app_path;
}

auto App::IsHbmenu() -> bool {
    return !strcasecmp(GetExePath().s, "/hbmenu.nro");
}

auto App::GetHddEnable() -> bool {
    return g_app->m_hdd_enabled.Get();
}

auto App::GetWriteProtect() -> bool {
    return g_app->m_hdd_write_protect.Get();
}

auto App::GetHddShowSystemFiles() -> bool {
    return g_app->m_hdd_show_system_files.Get();
}

auto App::GetHddIgnoreHibernation() -> bool {
    return g_app->m_hdd_ignore_hibernation.Get();
}

auto App::GetLogEnable() -> bool {
    return g_app->m_log_enabled.Get();
}

auto App::GetInstallEnable() -> bool {
    if (IsEmummc()) {
        return GetInstallEmummcEnable();
    } else {
        return GetInstallSysmmcEnable();
    }
}

auto App::GetInstallSysmmcEnable() -> bool {
    return g_app->m_install_sysmmc.GetOr("install");
}

auto App::GetInstallEmummcEnable() -> bool {
    return g_app->m_install_emummc.GetOr("install");
}

auto App::GetInstallSdEnable() -> bool {
    return g_app->m_install_sd.Get();
}

auto App::GetLanguage() -> long {
    return g_app->m_language.Get();
}

auto App::GetTextScrollSpeed() -> long {
    return g_app->m_text_scroll_speed.Get();
}

void App::SetHddEnable(bool enable) {
    if (App::GetHddEnable() != enable) {
        g_app->m_hdd_enabled.Set(enable);
#ifdef ENABLE_LIBUSBHSFS
        if (enable) {
            ApplyHddMountFlags();
            usbHsFsInitialize(1);
        } else {
            usbHsFsExit();
        }
#endif // ENABLE_LIBUSBHSFS
    }
}

void App::SetWriteProtect(bool enable) {
    if (App::GetWriteProtect() != enable) {
        g_app->m_hdd_write_protect.Set(enable);
        App::RemountHdd();
    }
}

void App::SetHddShowSystemFiles(bool enable) {
    if (App::GetHddShowSystemFiles() != enable) {
        g_app->m_hdd_show_system_files.Set(enable);
        App::RemountHdd();
    }
}

void App::SetHddIgnoreHibernation(bool enable) {
    if (App::GetHddIgnoreHibernation() != enable) {
        g_app->m_hdd_ignore_hibernation.Set(enable);
        App::RemountHdd();
    }
}

void App::RemountHdd() {
#ifdef ENABLE_LIBUSBHSFS
    ApplyHddMountFlags();

    // mount flags are only read when a device is mounted, so cycle the library
    // to pick them up, otherwise the user would have to unplug the drive.
    if (App::GetHddEnable()) {
        usbHsFsExit();
        usbHsFsInitialize(1);
    }
#endif // ENABLE_LIBUSBHSFS
}

void App::SetLogEnable(bool enable) {
    if (App::GetLogEnable() != enable) {
        g_app->m_log_enabled.Set(enable);
        if (enable) {
            log_file_init();
        } else {
            log_file_exit();
        }
    }
}

void App::SetInstallSysmmcEnable(bool enable) {
    g_app->m_install_sysmmc.Set(enable);
}

void App::SetInstallEmummcEnable(bool enable) {
    g_app->m_install_emummc.Set(enable);
}

void App::SetInstallSdEnable(bool enable) {
    g_app->m_install_sd.Set(enable);
}

void App::SetLanguage(long index) {
    if (App::GetLanguage() != index) {
        g_app->m_language.Set(index);
        on_i18n_change();

        App::Push<ui::OptionBox>(
            "Restart " APP_NAME "?"_i18n,
            "Back"_i18n, "Restart"_i18n, 1, [](auto op_index){
                if (op_index && *op_index) {
                    App::ExitRestart();
                }
            }
        );
    }
}

void App::SetTextScrollSpeed(long index) {
    g_app->m_text_scroll_speed.Set(index);
}

auto App::IsEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') || (paths.nintendo[0] != '\0');
}

auto App::IsParitionBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] == '\0') && (paths.nintendo[0] != '\0');
}

auto App::IsFileBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') && (paths.nintendo[0] != '\0');
}

void App::Exit() {
    g_app->m_quit = true;
}

void App::ExitToHomebrew() {
    const auto exit_to_homebrew = [] {
        constexpr fs::FsPath path{"/hbmenu.nro"};
        if (IsHbmenu()) {
            App::Push<ui::OptionBox>(APP_NAME " is installed as /hbmenu.nro. Restore the homebrew menu there to return to it.", "OK");
            return;
        }

        NroEntry entry;
        if (const auto rc = nro_parse(path, entry); R_FAILED(rc)) {
            App::PushErrorBox(rc, "Cannot open /hbmenu.nro. Restore the homebrew menu on the microSD card.");
            return;
        }
        // NXLoader was this app's name before SwoleLoader.
        if (!std::strncmp(entry.GetName(), APP_NAME, std::strlen(APP_NAME)) || !std::strncmp(entry.GetName(), "NXLoader", 8)) {
            App::Push<ui::OptionBox>("/hbmenu.nro is another " APP_NAME " build. Restore the homebrew menu there to return to it.", "OK");
            return;
        }

        if (const auto rc = nro_launch(path.toString()); R_FAILED(rc)) {
            App::PushErrorBox(rc, "The loader could not return to the homebrew menu.");
            return;
        }
        __nx_applet_exit_mode = 0;
        Exit();
    };

    exit_to_homebrew();
}

void App::ExitRestart() {
    nro_launch(GetExePath());
    Exit();
}

void App::Poll() {
    m_controller.Reset();

    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
    m_touch_info.is_clicked = false;

    // todo:
    #if 0
    HidMouseState mouse_state{};
    hidGetMouseStates(&mouse_state, 1);

    if (mouse_state.buttons) {
        log_write("[MOUSE] buttons: 0x%X x: %d y: %d dx: %d dy: %d wx: %d wy: %d\n", mouse_state.buttons, mouse_state.x, mouse_state.y, mouse_state.delta_x, mouse_state.delta_y, mouse_state.wheel_delta_x, mouse_state.wheel_delta_y);
    }
    #endif

// todo: replace old touch code with gestures from below
#if 0
    static HidGestureState prev_gestures[17]{};
    HidGestureState gestures[17]{};
    const auto gesture_count = hidGetGestureStates(gestures, std::size(gestures));
    for (int i = (int)gesture_count - 1; i >= 0; i--) {
        bool found = false;
        for (int j = 0; j < gesture_count; j++) {
            if (gestures[i].type == prev_gestures[j].type && gestures[i].sampling_number == prev_gestures[j].sampling_number) {
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        auto gesture = gestures[i];
        if (gesture_count && gesture.type == HidGestureType_Touch) {
            log_write("[TOUCH] got gesture attr: %u direction: %u sampling_number: %zu context_number: %zu\n", gesture.attributes, gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Swipe) {
            log_write("[SWIPE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Tap) {
            log_write("[TAP] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Press) {
            log_write("[PRESS] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Cancel) {
            log_write("[CANCEL] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Complete) {
            log_write("[COMPLETE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Pan) {
            log_write("[PAN] got gesture direction: %u sampling_number: %zu context_number: %zu x: %d y: %d dx: %d dy: %d vx: %.2f vy: %.2f count: %d\n", gesture.direction, gesture.sampling_number, gesture.context_number, gesture.x, gesture.y, gesture.delta_x, gesture.delta_y, gesture.velocity_x, gesture.velocity_y, gesture.point_count);
        }
    }

    memcpy(prev_gestures, gestures, sizeof(gestures));
#endif

    // todo: support mouse scroll / touch.
    if (state.count == 1 && !m_touch_info.is_touching) {
        m_touch_info.initial = m_touch_info.cur = state.touches[0];
        m_touch_info.is_touching = true;
        m_touch_info.is_tap = true;
    } else if (state.count >= 1 && m_touch_info.is_touching) {
        m_touch_info.cur = state.touches[0];

        if (m_touch_info.is_tap &&
            (std::abs((s32)m_touch_info.initial.x - (s32)m_touch_info.cur.x) > 20 ||
            std::abs((s32)m_touch_info.initial.y - (s32)m_touch_info.cur.y) > 20)) {
            m_touch_info.is_tap = false;
            m_touch_info.is_scroll = true;
        }
    } else if (m_touch_info.is_touching) {
        m_touch_info.is_touching = false;
        m_touch_info.is_scroll = false;
        if (m_touch_info.is_tap) {
            m_touch_info.is_clicked = true;
        } else {
            m_touch_info.is_end = true;
        }
    }

    u64 kdown = 0;
    u64 kup = 0;
    u64 kheld = 0;

    // todo: better implement this to match hos
    if (!m_touch_info.is_touching && !m_touch_info.is_clicked) {
        // controller.
        padUpdate(&m_pad);
        kdown |= padGetButtonsDown(&m_pad);
        kheld |= padGetButtons(&m_pad);
        kup |= padGetButtonsUp(&m_pad);

        // keyboard.
        m_keyboard.Update();
        kdown |= m_keyboard.GetButtonsDown();
        kheld |= m_keyboard.GetButtons();
        kup |= m_keyboard.GetButtonsUp();
    }

    m_controller.m_kdown = kdown;
    m_controller.m_kheld = kheld;
    m_controller.m_kup = kup;
    m_controller.UpdateButtonHeld(static_cast<u64>(Button::ANY_DIRECTION), m_delta_time);
}

void App::Update() {
    const bool has_wake_input = m_controller.m_kdown || m_touch_info.is_touching || m_touch_info.is_clicked;
    if (has_wake_input) {
        m_last_input_ns = armTicksToNs(armGetSystemTick());
    }
    if (has_wake_input && App::IsScreenToggleEnabled() && App::IsScreenDisabled()) {
        App::SetScreenDisabled(false);

        // The input which wakes the display must not activate the hidden UI.
        m_controller.Reset();
        m_controller.m_kheld = 0;
        m_touch_info.is_tap = false;
        m_touch_info.is_scroll = false;
        m_touch_info.is_clicked = false;
        m_touch_info.is_end = false;
    }

    m_widgets.back()->Update(&m_controller, &m_touch_info);

    bool popped_at_least1 = false;
    while (true) {
        if (m_widgets.empty()) {
            log_write("[Mui] no widgets left, so we exit...");
            App::Exit();
            return;
        }

        if (m_widgets.back()->ShouldPop()) {
            log_write("popping widget\n");
            m_widgets.pop_back();
            popped_at_least1 = true;
        } else {
            break;
        }
    }

    if (!m_widgets.empty() && popped_at_least1) {
        m_widgets.back()->OnFocusGained();
    }

}

void App::Draw() {
    const auto slot = this->queue.acquireImage(this->swapchain);
    this->queue.submitCommands(this->framebuffer_cmdlists[slot]);
    this->queue.submitCommands(this->render_cmdlist);
    nvgBeginFrame(this->vg, s_width, s_height, 1.f);
    nvgScale(vg, m_scale.x, m_scale.y);

    // find the last menu in the list, start drawing from there
    auto menu_it = m_widgets.rend();
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); it++) {
        const auto& p = *it;
        if (!p->IsHidden() && p->IsMenu()) {
            menu_it = it;
            break;
        }
    }

    // reverse itr so loop backwards to go forwarders.
    if (menu_it != m_widgets.rend()) {
        for (auto it = menu_it; ; it--) {
            const auto& p = *it;

            // draw everything not hidden on top of the menu.
            if (!p->IsHidden()) {
                p->Draw(vg, &m_theme);
            }

            if (it == m_widgets.rbegin()) {
                break;
            }
        }
    }

    m_notif_manager.Draw(vg, &m_theme);

    nvgResetTransform(vg);
    nvgEndFrame(this->vg);
    this->queue.presentImage(this->swapchain, slot);
}

auto App::GetApp() -> App* {
    return g_app;
}

auto App::AcquireScreenToggle() -> bool {
    SCOPED_MUTEX(&g_screen_toggle_mutex);

    if (!g_screen_toggle_ref_count) {
        const auto rc = lblInitialize();
        if (R_FAILED(rc)) {
            log_write("[SCREEN] lblInitialize() failed: 0x%X\n", R_VALUE(rc));
            return false;
        }

        g_screen_disabled = false;
    }

    g_screen_toggle_ref_count++;
    return true;
}

void App::ReleaseScreenToggle() {
    SCOPED_MUTEX(&g_screen_toggle_mutex);

    if (!g_screen_toggle_ref_count) {
        return;
    }

    g_screen_toggle_ref_count--;
    if (!g_screen_toggle_ref_count) {
        const auto rc = appletSetLcdBacklightOffEnabled(false);
        if (R_FAILED(rc)) {
            log_write("[SCREEN] failed to restore backlight: 0x%X\n", R_VALUE(rc));
        }

        g_screen_disabled = false;
        lblExit();
    }
}

auto App::IsScreenToggleEnabled() -> bool {
    SCOPED_MUTEX(&g_screen_toggle_mutex);
    return g_screen_toggle_ref_count != 0;
}

auto App::IsScreenDisabled() -> bool {
    SCOPED_MUTEX(&g_screen_toggle_mutex);

    if (!g_screen_toggle_ref_count) {
        return false;
    }

    LblBacklightSwitchStatus status{};
    if (R_SUCCEEDED(lblGetBacklightSwitchStatus(&status))) {
        g_screen_disabled = status != LblBacklightSwitchStatus_Enabled;
    }

    return g_screen_disabled;
}

auto App::IsScreenOff() -> bool {
    SCOPED_MUTEX(&g_screen_toggle_mutex);

    if (!g_screen_toggle_ref_count || !g_screen_disabled) {
        return false;
    }

    LblBacklightSwitchStatus status{};
    if (R_SUCCEEDED(lblGetBacklightSwitchStatus(&status))) {
        g_screen_disabled = status != LblBacklightSwitchStatus_Enabled;
    }

    return g_screen_disabled;
}

auto App::GetLastInputNs() -> u64 {
    return g_app->m_last_input_ns;
}

auto App::GetAutoScreenOffSeconds() -> u64 {
    // matches the choices in DisplayAdvancedOptions.
    static constexpr u64 seconds[]{0, 30, 60, 120, 300};
    const auto index = g_app->m_auto_screen_off.Get();
    return index >= 0 && index < static_cast<long>(std::size(seconds)) ? seconds[index] : 60;
}

auto App::GetBattery() -> nxloader::Battery {
    static Mutex mutex{};
    static bool tried{}, ready{};
    SCOPED_MUTEX(&mutex);

    if (!tried) {
        tried = true;
        const auto rc = psmInitialize();
        ready = R_SUCCEEDED(rc);
        if (!ready) {
            log_write("[BATTERY] psmInitialize() failed: 0x%X\n", rc);
        }
    }

    nxloader::Battery battery{};
    u32 percent{};
    PsmChargerType charger{};
    if (ready && R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)) && R_SUCCEEDED(psmGetChargerType(&charger))) {
        battery = {percent, charger == PsmChargerType_EnoughPower, true};
    }
    return battery;
}

void App::SetScreenDisabled(bool disable) {
    SCOPED_MUTEX(&g_screen_toggle_mutex);

    if (!g_screen_toggle_ref_count) {
        return;
    }

    const auto rc = appletSetLcdBacklightOffEnabled(disable);
    if (R_FAILED(rc)) {
        log_write("[SCREEN] failed to set backlight state: 0x%X\n", R_VALUE(rc));
        return;
    }

    g_screen_disabled = disable;
}

auto App::GetVg() -> NVGcontext* {
    return g_app->vg;
}

void DrawElement(float x, float y, float w, float h, ThemeEntryID id) {
    DrawElement({x, y, w, h}, id);
}

void DrawElement(const Vec4& v, ThemeEntryID id) {
    const auto& e = g_app->m_theme.elements[id];

    switch (e.type) {
        case ElementType::None: {
        } break;
        case ElementType::Texture: {
            auto paint = nvgImagePattern(g_app->vg, v.x, v.y, v.w, v.h, 0, e.texture, 1.f);
            // override the icon colours if set
            if (id > ThemeEntryID_ICON_COLOUR && id < ThemeEntryID_MAX) {
                if (g_app->m_theme.elements[ThemeEntryID_ICON_COLOUR].type != ElementType::None) {
                    paint.innerColor = g_app->m_theme.GetColour(ThemeEntryID_ICON_COLOUR);
                }
            }
            ui::gfx::drawRect(g_app->vg, v, paint);
        } break;
        case ElementType::Colour: {
            ui::gfx::drawRect(g_app->vg, v, e.colour);
        } break;
    }
}

auto App::LoadElementImage(std::string_view value) -> ElementEntry {
    ElementEntry entry{};

    entry.texture = nvgCreateImage(vg, value.data(), 0);
    if (entry.texture) {
        entry.type = ElementType::Texture;
    }

    return entry;
}

auto App::LoadElementColour(std::string_view value) -> ElementEntry {
    ElementEntry entry{};

    if (value.starts_with("0x")) {
        value = value.substr(2);
    } else {
        return {};
    }

    char* end;
    u32 c = std::strtoul(value.data(), &end, 16);
    if (!c && value.data() == end) {
        return {};
    }

    // force alpha bit if not already set.
    if (value.length() <= 6) {
        c <<= 8;
        c |= 0xFF;
    }

    entry.colour = nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    entry.type = ElementType::Colour;
    return entry;
}

auto App::LoadElement(std::string_view value, ElementType type) -> ElementEntry {
    if (value.size() <= 1) {
        return {};
    }

    if (type == ElementType::None || type == ElementType::Colour) {
        // most assets are colours, so prioritise this first
        if (auto e = LoadElementColour(value); e.type != ElementType::None) {
            return e;
        }
    }

    if (type == ElementType::None || type == ElementType::Texture) {
        if (auto e = LoadElementImage(value); e.type != ElementType::None) {
            return e;
        }
    }

    return {};
}

void App::CloseTheme() {
    for (auto& e : m_theme.elements) {
        if (e.type == ElementType::Texture) {
            nvgDeleteImage(vg, e.texture);
        }
    }

    m_theme = {};
}

void App::LoadTheme(const ThemeMeta& meta) {
    // reset theme
    CloseTheme();

    ThemeData theme_data{};
    LoadThemeInternal(meta, theme_data);
    m_theme.meta = meta;

    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());

        // load all assets / colours.
        for (auto& e : THEME_ENTRIES) {
            m_theme.elements[e.id] = LoadElement(theme_data.elements[e.id], e.type);
        }
    }
}

Result App::GetSdSize(s64* free, s64* total) {
    fs::FsNativeContentStorage fs{FsContentStorageId_SdCard};
    R_TRY(fs.GetFreeSpace("/", free));
    R_TRY(fs.GetTotalSpace("/", total));
    R_SUCCEED();
}

Result App::GetEmmcSize(s64* free, s64* total) {
    fs::FsNativeContentStorage fs{FsContentStorageId_User};
    R_TRY(fs.GetFreeSpace("/", free));
    R_TRY(fs.GetTotalSpace("/", total));
    R_SUCCEED();
}

App::App(const char* argv0) {
    // boost mode is enabled in userAppInit().
    ON_SCOPE_EXIT(App::SetBoostMode(false));
    SCOPED_TIMESTAMP("App Constructor");

    g_app = this;
    m_start_timestamp = armGetSystemTick();
    m_last_input_ns = armTicksToNs(m_start_timestamp);
    if (!std::strncmp(argv0, "sdmc:/", 6)) {
        // memmove(path, path + 5, strlen(path)-5);
        std::strncpy(m_app_path, argv0 + 5, std::strlen(argv0)-5);
    } else {
        m_app_path = argv0;
    }

    // set if we are hbmenu
    if (IsHbmenu()) {
        __nx_applet_exit_mode = 1;
    }

    // init fs for app use.
    m_fs = std::make_shared<fs::FsNativeSd>(true);

    static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto app = static_cast<App*>(UserData);

        if (!std::strcmp(Section, INI_SECTION)) {
            if (app->m_hdd_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_write_protect.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_show_system_files.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_ignore_hibernation.LoadFrom(Key, Value)) {}
            else if (app->m_log_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_show_ip_addr.LoadFrom(Key, Value)) {}
            else if (app->m_language.LoadFrom(Key, Value)) {}
            else if (app->m_install_sysmmc.LoadFrom(Key, Value)) {}
            else if (app->m_install_emummc.LoadFrom(Key, Value)) {}
            else if (app->m_install_sd.LoadFrom(Key, Value)) {}
            else if (app->m_selection_details.LoadFrom(Key, Value)) {}
            else if (app->m_update_check_on_launch.LoadFrom(Key, Value)) {}
            else if (app->m_progress_boost_mode.LoadFrom(Key, Value)) {}
            else if (app->m_boost_compressed_only.LoadFrom(Key, Value)) {}
            else if (app->m_auto_screen_off.LoadFrom(Key, Value)) {}
            else if (app->m_battery_guard.LoadFrom(Key, Value)) {}
            else if (app->m_allow_downgrade.LoadFrom(Key, Value)) {}
            else if (app->m_skip_if_already_installed.LoadFrom(Key, Value)) {}
            else if (app->m_ticket_only.LoadFrom(Key, Value)) {}
            else if (app->m_skip_base.LoadFrom(Key, Value)) {}
            else if (app->m_skip_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_addon.LoadFrom(Key, Value)) {}
            else if (app->m_skip_data_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_skip_nca_hash_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_header_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_npdm_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_ignore_distribution_bit.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_common_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_standard_crypto.LoadFrom(Key, Value)) {}
            else if (app->m_lower_master_key.LoadFrom(Key, Value)) {}
            else if (app->m_lower_system_version.LoadFrom(Key, Value)) {}
        } else if (!std::strcmp(Section, "accessibility")) {
            if (app->m_text_scroll_speed.LoadFrom(Key, Value)) {}
        }

        return 1;
    };

    MigrateOldConfig();

    // load all configs ahead of time, as this is actually faster than
    // loading each config one by one as it avoids re-opening the file multiple times.
    {
        SCOPED_TIMESTAMP("config init");
        ini_browse(cb, this, CONFIG_PATH);
    }

    if (App::GetLogEnable()) {
        log_file_init();
        LogSessionHeader();
    }

    // before anything can start a new install.
    yati::CleanupInterruptedInstalls();

    // anything that can be async loaded should be placed in here in order
    // to halve load times.
    // rules:
    // - 1: cannot use romfs as its not thread-safe.
    // - 2: cannot use nvg code as its not thread-safe.
    // - 3: cannot be too slow that async takes longer than the main thread (ie, balance the load).
    // currrent load time is 60ms without logs, 90 with (down from 230ms).
    utils::Async async_init([this](){
        SCOPED_TIMESTAMP("App async load");

        {
            SCOPED_TIMESTAMP("config directory init");
            m_fs->CreateDirectoryRecursively(APP_CONFIG_DIR);
            m_fs->CreateDirectory(APP_CACHE_DIR);
            m_fs->CreateDirectory(APP_CONFIG_DIR "/assoc");
            m_fs->CreateDirectory(APP_CONFIG_DIR "/themes");
            m_fs->CreateDirectory(APP_CONFIG_DIR "/github");
            m_fs->CreateDirectory(APP_CONFIG_DIR "/i18n");
            m_fs->CreateDirectory(APP_CONFIG_DIR "/mount");

            if (!ini_haskey("steamgriddb", "api_key", CONFIG_PATH)) {
                ini_puts("steamgriddb", "api_key", "", CONFIG_PATH);
            }
        }

        if (log_is_init()) {
            SCOPED_TIMESTAMP("fw log init");
            SetSysFirmwareVersion fw_version{};
            setsysInitialize();
            ON_SCOPE_EXIT(setsysExit());
            setsysGetFirmwareVersion(&fw_version);

            log_write("[version] platform: %s\n", fw_version.platform);
            log_write("[version] version_hash: %s\n", fw_version.version_hash);
            log_write("[version] display_version: %s\n", fw_version.display_version);
            log_write("[version] display_title: %s\n", fw_version.display_title);

            splInitialize();
            ON_SCOPE_EXIT(splExit());

            u64 out{};
            splGetConfig((SplConfigItem)65000, &out);
            log_write("[ams] version: %lu.%lu.%lu\n", (out >> 56) & 0xFF, (out >> 48) & 0xFF, (out >> 40) & 0xFF);
            log_write("[ams] target version: %lu.%lu.%lu\n", (out >> 24) & 0xFF, (out >> 16) & 0xFF, (out >> 8) & 0xFF);
            log_write("[ams] key gen: %lu\n", (out >> 32) & 0xFF);

            splGetConfig((SplConfigItem)65003, &out);
            log_write("[ams] hash: %lx\n", out);

            splGetConfig((SplConfigItem)65010, &out);
            log_write("[ams] usb 3.0 enabled: %lu\n", out);
        }

        // get emummc config.
        {
            SCOPED_TIMESTAMP("emummc detect init");
            alignas(0x1000) AmsEmummcPaths paths{};
            SecmonArgs args{};
            args.X[0] = 0xF0000404; /* smcAmsGetEmunandConfig */
            args.X[1] = 0; /* EXO_EMUMMC_MMC_NAND*/
            args.X[2] = (u64)&paths; /* out path */
            svcCallSecureMonitor(&args);

            // exosphere copies the nintendo path for every type, including none,
            // so only keep the paths if emummc is actually active.
            // x1 holds the base config: magic (EFS0) in the low and the type in the high 32 bits.
            const u32 magic = static_cast<u32>(args.X[1]);
            const u32 type = static_cast<u32>(args.X[1] >> 32);
            log_write("[emummc] result: 0x%lX magic: 0x%X type: %u\n", args.X[0], magic, type);
            if (args.X[0] == 0 && magic == 0x30534645 && type != 0) {
                m_emummc_paths = paths;
            }

            log_write("[emummc] enabled: %u\n", App::IsEmummc());
            if (App::IsEmummc()) {
                log_write("[emummc] file based path: %s\n", m_emummc_paths.file_based_path);
                log_write("[emummc] nintendo path: %s\n", m_emummc_paths.nintendo);
            }
        }

#ifdef ENABLE_LIBUSBHSFS
        if (App::GetHddEnable()) {
            SCOPED_TIMESTAMP("hdd init");
            ApplyHddMountFlags();
            usbHsFsInitialize(1);
        }
#endif // ENABLE_LIBUSBHSFS

        {
            SCOPED_TIMESTAMP("curl init");
            curl::Init();
        }

        {
            SCOPED_TIMESTAMP("HID init");
            hidInitializeTouchScreen();
            hidInitializeGesture();
            hidInitializeKeyboard();
            hidInitializeMouse();

            padConfigureInput(8, HidNpadStyleSet_NpadStandard);
            // padInitializeDefault(&m_pad);
            padInitializeAny(&m_pad);

            m_keyboard.Init(KEYBOARD_BUTTON_MAP);
        }

    });

    std::string language_name;
    {
        SCOPED_TIMESTAMP("i18n init");
        language_name = i18n::init(GetLanguage());
    }

#ifdef USE_NVJPG
    {
        SCOPED_TIMESTAMP("nvjpg init");
        // this has to be init before deko3d.
        nj::initialize();
        m_decoder.initialize();
    }
#endif

    // get current size of the framebuffer
    {
        SCOPED_TIMESTAMP("nvg init");
        const auto fb = GetFrameBufferSize();
        s_width = fb.size.x;
        s_height = fb.size.y;
        m_scale = fb.scale;

        // Create the deko3d device
        this->device = dk::DeviceMaker{}
            .setCbDebug(deko3d_error_cb)
            .create();

        // Create the main queue
        this->queue = dk::QueueMaker{this->device}
            .setFlags(DkQueueFlags_Graphics)
            .create();

        // Create the memory pools
        this->pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
        this->pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
        this->pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

        // Create the static command buffer and feed it freshly allocated memory
        this->cmdbuf = dk::CmdBufMaker{this->device}.create();
        const CMemPool::Handle cmdmem = this->pool_data->allocate(this->StaticCmdSize);
        this->cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

        // Create the framebuffer resources
        this->createFramebufferResources();

        this->renderer.emplace(s_width, s_height, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
        this->vg = nvgCreateDk(&*this->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    }

    // not sure if these are meant to be deleted or not...
    {
        SCOPED_TIMESTAMP("font init");
        PlSharedFontType standard_font_type = PlSharedFontType_Standard;
        std::vector<PlSharedFontType> lang_fonts = {
            PlSharedFontType_ChineseSimplified,
            PlSharedFontType_ExtChineseSimplified,
            PlSharedFontType_ChineseTraditional,
            PlSharedFontType_Standard,
        };
        if (language_name == "zh-Hans")
        {
            standard_font_type = PlSharedFontType_ChineseSimplified;
            std::erase(lang_fonts, PlSharedFontType_ChineseSimplified);
        }
        else if (language_name == "zh-Hant")
        {
            standard_font_type = PlSharedFontType_ChineseTraditional;
            std::erase(lang_fonts, PlSharedFontType_ChineseTraditional);
        }
        else
        {
            std::erase(lang_fonts, PlSharedFontType_Standard);
        }
        lang_fonts.push_back(PlSharedFontType_KO);

        PlFontData font_standard, font_extended, font_lang;
        plGetSharedFontByType(&font_standard, standard_font_type);
        plGetSharedFontByType(&font_extended, PlSharedFontType_NintendoExt);

        auto standard_font = nvgCreateFontMem(this->vg, "Standard", (unsigned char*)font_standard.address, font_standard.size, 0);
        auto extended_font = nvgCreateFontMem(this->vg, "Extended", (unsigned char*)font_extended.address, font_extended.size, 0);
        nvgAddFallbackFontId(this->vg, standard_font, extended_font);

        for (auto type : lang_fonts) {
            if (R_SUCCEEDED(plGetSharedFontByType(&font_lang, type))) {
                char name[32];
                std::snprintf(name, sizeof(name), "Lang_%u", font_lang.type);
                auto lang_font = nvgCreateFontMem(this->vg, name, (unsigned char*)font_lang.address, font_lang.size, 0);
                nvgAddFallbackFontId(this->vg, standard_font, lang_font);
            } else {
                log_write("failed plGetSharedFontByType(%d)\n", type);
            }
        }
    }

    {
        SCOPED_TIMESTAMP("hook init");
        appletHook(&m_appletHookCookie, appplet_hook_calback, this);
    }

    // load default image
    {
        SCOPED_TIMESTAMP("load default image");
        m_default_image = nvgCreateImageMem(vg, 0, DEFAULT_IMAGE_DATA, std::size(DEFAULT_IMAGE_DATA));
    }

    // disable audio in applet mode with a suspended application due to audren fatal.
    // see: https://github.com/ITotalJustice/sphaira/issues/92
    if (IsAppletWithSuspendedApp()) {
        App::Notify("Audio disabled due to suspended game"_i18n);
    } else {
        SCOPED_TIMESTAMP("audio init");
        if (R_FAILED(audio::Init())) {
            log_write("[AUDIO] failed to init\n");
        }
    }

    {
        SCOPED_TIMESTAMP("theme init");
        ThemeMeta theme_meta;
        if (R_SUCCEEDED(romfsInit())) {
            ON_SCOPE_EXIT(romfsExit());
            LoadThemeMeta(DEFAULT_THEME_PATH, theme_meta);
        }
        LoadTheme(theme_meta);
    }
}

void App::PlaySoundEffect(SoundEffect effect) {
    audio::PlaySoundEffect(effect);
}

void App::DisplayLogOptions() {
    auto options = std::make_unique<ui::Sidebar>("Logs"_i18n, ui::Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    static constexpr struct {
        const char* title;
        const char* path;
        const char* info;
    } LOGS[]{
        { "This session", LOG_DIRECTORY "/log.txt", "What the app has done since it was opened." },
        { "Previous session", LOG_DIRECTORY "/previous.txt", "The log from the last time the app was open. Check this one after a crash or freeze." },
        { "Last crash report", LOG_DIRECTORY "/crash.txt", "Details of the most recent crash. tools/symbolize.sh turns its addresses into source locations." },
    };

    for (const auto& e : LOGS) {
        options->Add<ui::SidebarEntryCallback>(i18n::get(e.title), [path = e.path](){
            log_flush();
            if (!g_app->m_fs->FileExists(path)) {
                App::Notify("Nothing has been saved there yet."_i18n);
                return;
            }
            App::Push<ui::menu::fileview::Menu>(g_app->m_fs.get(), path);
        }, i18n::get(e.info) + "\n\n" + e.path);
    }
}

void App::DisplayAdvancedOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Advanced Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items text_scroll_speed_items;
    text_scroll_speed_items.push_back("Slow"_i18n);
    text_scroll_speed_items.push_back("Normal"_i18n);
    text_scroll_speed_items.push_back("Fast"_i18n);

    options->Add<ui::SidebarEntryBool>("Logging"_i18n, App::GetLogEnable(), [](bool& enable){
        App::SetLogEnable(enable);
    }, "Records what the app is doing, to help find the cause of problems. "
        "Saved in " LOG_DIRECTORY ": log.txt for this session and previous.txt for the one before. "
        "Crash reports are saved to crash.txt even when this is off."_i18n);

    options->Add<ui::SidebarEntryCallback>("View logs"_i18n, [](){
        App::DisplayLogOptions();
    }, "Open this session's log, the previous session's log, or the last crash report."_i18n);

    options->Add<ui::SidebarEntryBool>("Check for updates on launch"_i18n, App::GetApp()->m_update_check_on_launch,
        "Checks GitHub for a newer version each time the app starts. Updates are only installed when you choose to."_i18n);

    options->Add<ui::SidebarEntryBool>("Boost CPU during transfer"_i18n, App::GetApp()->m_progress_boost_mode,
        i18n::get("transfer_boost_info",
            "Enables boost mode during transfers which can improve transfer speed. "
            "This sets the CPU to 1785mhz and lowers the GPU 76mhz"));

    options->Add<ui::SidebarEntryBool>("Boost only for compressed games"_i18n, App::GetApp()->m_boost_compressed_only,
        "Boosts only while NSZ/XCZ content is unpacked, which needs the faster CPU. "
        "Plain NSP/XCI installs and copies are limited by the drive, so boosting them mostly costs battery. "
        "Turn this off to boost every transfer, as before."_i18n);

    ui::SidebarEntryArray::Items screen_off_items;
    screen_off_items.push_back("Never"_i18n);
    screen_off_items.push_back("After 30 seconds"_i18n);
    screen_off_items.push_back("After 1 minute"_i18n);
    screen_off_items.push_back("After 2 minutes"_i18n);
    screen_off_items.push_back("After 5 minutes"_i18n);
    options->Add<ui::SidebarEntryArray>("Screen off during installs"_i18n, screen_off_items, [](s64& index_out){
        App::GetApp()->m_auto_screen_off.Set(index_out);
    }, App::GetApp()->m_auto_screen_off.Get(),
        "Turns the screen off during installs and other long tasks when nothing has been pressed for a while. "
        "The task keeps running; press any button or touch the screen to turn it back on. "
        "Handheld only, the screen is the biggest use of battery."_i18n);

    options->Add<ui::SidebarEntryArray>("Text scroll speed"_i18n, text_scroll_speed_items, [](s64& index_out){
        App::SetTextScrollSpeed(index_out);
    }, App::GetTextScrollSpeed(), "Change how fast the scrolling text updates"_i18n);

    options->Add<ui::SidebarEntryCallback>("Install options"_i18n, [left_side](){
        App::DisplayInstallOptions(left_side);
    },  i18n::get("install_options_info",
            "Change the install options.\n"
            "You can enable installing from here."));
}

void App::DisplayInstallOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Install Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items install_items;
    install_items.push_back("System memory"_i18n);
    install_items.push_back("microSD card"_i18n);

    options->Add<ui::SidebarEntryBool>("Enable sysmmc"_i18n, App::GetInstallSysmmcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_sysmmc, enable);
    }, "Enables installing whilst in sysMMC mode."_i18n);

    options->Add<ui::SidebarEntryBool>("Enable emummc"_i18n, App::GetInstallEmummcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_emummc, enable);
    }, "Enables installing whilst in emuMMC mode."_i18n);

    options->Add<ui::SidebarEntryArray>("Install location"_i18n, install_items, [](s64& index_out){
        App::SetInstallSdEnable(index_out);
    }, (s64)App::GetInstallSdEnable());

    options->Add<ui::SidebarEntryBool>("Show selection size details", App::GetApp()->m_selection_details,
        "Show additional source and installed-size totals below the file list.");

    options->Add<ui::SidebarEntryBool>("Stop when battery is low"_i18n, App::GetApp()->m_battery_guard,
        "During a batch install on battery, no new game is started at 15% and the current one is cancelled at 5%, "
        "so a game is never cut off by the console shutting down. Asks before starting below 30%. "
        "Does nothing on a full-power charger or the dock."_i18n);

    options->Add<ui::SidebarEntryBool>("Allow downgrade"_i18n, App::GetApp()->m_allow_downgrade,
        "Allows for installing title updates that are lower than the currently installed update."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip if already installed"_i18n, App::GetApp()->m_skip_if_already_installed,
        "Skips installing titles / ncas if they're already installed."_i18n);

    options->Add<ui::SidebarEntryBool>("Ticket only"_i18n, App::GetApp()->m_ticket_only,
        "Installs tickets only, useful if the title was already installed however the tickets were missing or corrupted."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip base"_i18n, App::GetApp()->m_skip_base,
        "Skips installing the base application."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip patch"_i18n, App::GetApp()->m_skip_patch,
        "Skips installing updates."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip dlc"_i18n, App::GetApp()->m_skip_addon,
        "Skips installing DLC."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip data patch"_i18n, App::GetApp()->m_skip_data_patch,
        "Skips installing DLC update (data patch)."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip ticket"_i18n, App::GetApp()->m_skip_ticket,
        "Skips installing tickets, not recommended."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip NCA hash verify"_i18n, App::GetApp()->m_skip_nca_hash_verify,
        i18n::get("skip_nca_info",
            "Enables the option to skip sha256 verification. This is a hash over the entire NCA. "
            "It is used to verify that the NCA is valid / not corrupted. "
            "You may have seen the option for \"checking for corrupted data\" when a corrupted game is installed. "
            "That check performs various hash checks, including the hash over the NCA.\n\n"
            "It is recommended to keep this disabled."));

    options->Add<ui::SidebarEntryBool>("Skip RSA header verify"_i18n, App::GetApp()->m_skip_rsa_header_fixed_key_verify,
        i18n::get("nca_verify_info",
            "Enables the option to skip RSA NCA fixed key verification. "
            "This is a hash over the NCA header. It is used to verify that the header has not been modified. "
            "The header is signed by nintendo, thus it cannot be forged, and is reliable to detect modified NCA headers (such as NSP/XCI converts).\n\n"
            "It is recommended to keep this disabled, unless you need to install nsp/xci converts."));

    options->Add<ui::SidebarEntryBool>("Skip RSA NPDM verify"_i18n, App::GetApp()->m_skip_rsa_npdm_fixed_key_verify,
        i18n::get("npdm_verify_info",
            "Enables the option to skip RSA NPDM fixed key verification.\n\n"
            "Currently, this option is stubbed (not implemented)."));

    options->Add<ui::SidebarEntryBool>("Ignore distribution bit"_i18n, App::GetApp()->m_ignore_distribution_bit,
        i18n::get("nca_distbit_info",
            "If set, it will ignore the distribution bit in the NCA header. "
            "The distribution bit is used to signify whether a NCA is Eshop or GameCard. "
            "You cannot (normally) launch install games that have the distruction bit set to GameCard.\n\n"
            "It is recommended to keep this disabled."));

    options->Add<ui::SidebarEntryBool>("Convert to common ticket"_i18n, App::GetApp()->m_convert_to_common_ticket,
        i18n::get("ticket_convert_info",
            "[Requires keys] Converts personalised tickets to common (fake) tickets.\n\n"
            "It is recommended to keep this enabled."));

    options->Add<ui::SidebarEntryBool>("Convert to standard crypto"_i18n, App::GetApp()->m_convert_to_standard_crypto,
        i18n::get("titlekey_crypto_info",
            "[Requires keys] Converts titlekey to standard crypto, also known as \"ticketless\".\n\n"
            "It is recommended to keep this disabled."));

    options->Add<ui::SidebarEntryBool>("Lower master key"_i18n, App::GetApp()->m_lower_master_key,
        i18n::get("keyarea_crypto_info",
            "[Requires keys] Encrypts the keak (key area key) with master key 0, which allows the game to be launched on every fw. "
            "Implicitly performs standard crypto.\n\n"
            "Do note that just because the game can be launched on any fw (as it can be decrypted), doesn't mean it will work. It is strongly recommened to update your firmware and Atmosphere version in order to play the game, rather than enabling this option.\n\n"
            "It is recommended to keep this disabled."));

    options->Add<ui::SidebarEntryBool>("Lower system version"_i18n, App::GetApp()->m_lower_system_version,
        i18n::get("cnmt_fw_info",
            "Sets the system_firmware field in the cnmt extended header to 0. "
            "Note: if the master key is higher than fw version, the game still won't launch as the fw won't have the key to decrypt keak (see above).\n\n"
            "It is recommended to keep this disabled."));
}

void App::DisplayHddOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("HDD Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<ui::SidebarEntryBool>("Enable"_i18n, App::GetHddEnable(), [](bool& enable){
        App::SetHddEnable(enable);
    },  i18n::get("mount_hdd_info",
            "Enable mounting of connected USB/HDD devices. "
            "Connected devices can be used in the FileBrowser, as well as a backup location when dumping games and saves."
        )
    );

    options->Add<ui::SidebarEntryBool>("HDD write protect"_i18n, App::GetWriteProtect(), [](bool& enable){
        App::SetWriteProtect(enable);
    },  "Makes the connected HDD read-only."_i18n);

#ifdef ENABLE_NTFS
    options->Add<ui::SidebarEntryBool>("Show system files"_i18n, App::GetHddShowSystemFiles(), [](bool& enable){
        App::SetHddShowSystemFiles(enable);
    },  i18n::get("hdd_show_system_files_info",
            "NTFS only. Shows entries that Windows marked as system files, such as $Recycle.Bin. "
            "Leave this off unless you know what you are doing."
        )
    );

    options->Add<ui::SidebarEntryBool>("Mount hibernated NTFS"_i18n, App::GetHddIgnoreHibernation(), [](bool& enable){
        App::SetHddIgnoreHibernation(enable);
    },  i18n::get("hdd_ignore_hibernation_info",
            "NTFS only. Mounts a drive that Windows left in a hibernated state (fast startup). "
            "WARNING: the saved Windows session on that drive is lost."
        )
    );
#endif // ENABLE_NTFS
}

void App::ShowEnableInstallPrompt() {
    // warn the user the dangers of installing.
    App::Push<ui::OptionBox>(
        "Installing is disabled, enable now?"_i18n,
        "Back"_i18n, "Enable"_i18n, 0, [](auto op_index){
            if (op_index && *op_index) {
                // get the install option based on sysmmc/emummc.
                auto& option = IsEmummc() ? g_app->m_install_emummc : g_app->m_install_sysmmc;

                // dummy ref.
                static bool enable{};
                enable = true;

                return ShowEnableInstallPromptOption(option, enable);
            }
        }
    );
}

void App::ShowEnableInstallPromptOption(option::OptionBool& option, bool& enable) {
    if (enable) {
        // warn the user the dangers of installing.
        App::Push<ui::OptionBox>(
            "WARNING: Installing apps will lead to a ban!"_i18n,
            "Back"_i18n, "Enable"_i18n, 0, [&option, &enable](auto op_index){
                if (op_index && *op_index) {
                    option.Set(true);
                    App::Notify("Installing enabled!"_i18n);
                } else {
                    enable = false;
                }
            }
        );
    } else {
        option.Set(false);
    }
}

App::~App() {
    // boost mode is disabled in userAppExit().
    App::SetBoostMode(true);

    log_write("starting to exit\n");
    {
        SCOPED_TIMESTAMP("TOTAL EXIT");
        appletUnhook(&m_appletHookCookie);

        // async exit as these threads sleep every 100ms.
        {
            SCOPED_TIMESTAMP("async signal");
            curl::ExitSignal();
        }

        // this has to be called before any cleanup to ensure the lifetime of
        // nvg is still active as some widgets may need to free images.
        // clear in reverse order as the widgets are a stack.
        {
            SCOPED_TIMESTAMP("widget exit");
            while (!m_widgets.empty()) {
                m_widgets.pop_back();
            }
            nxloader_ui::SelectionSizer::WaitRetired();
            nxloader_ui::SelectionSizer::PollRetired();
        }

        utils::Async async_exit([this](){
            {
                SCOPED_TIMESTAMP("i18n_exit");
                i18n::exit();
            }

#ifdef ENABLE_LIBUSBHSFS
            {
                SCOPED_TIMESTAMP("hdd exit");
                usbHsFsExit();
            }
#endif // ENABLE_LIBUSBHSFS

            // do these last as they were signalled to exit.
            {
                SCOPED_TIMESTAMP("audio_exit");
                audio::Exit();
            }

            {
                SCOPED_TIMESTAMP("curl_exit");
                curl::Exit();
            }
        });

        // do not async close theme as it frees textures.
        {
            SCOPED_TIMESTAMP("theme exit");
            CloseTheme();
        }

        {
            SCOPED_TIMESTAMP("destroy frame buffer resources");
            this->destroyFramebufferResources();
        }

        {
            SCOPED_TIMESTAMP("nvg exit");
            nvgDeleteImage(vg, m_default_image);
            nvgDeleteDk(this->vg);
            this->renderer.reset();

    #ifdef USE_NVJPG
            m_decoder.finalize();
            nj::finalize();
    #endif
        }

    }

    if (App::GetLogEnable()) {
        log_write("closing log\n");
        log_file_exit();
    }
}

auto App::GetVersionFromString(const char* str) -> u32 {
    u32 major{}, minor{}, macro{};
    std::sscanf(str, "%u.%u.%u", &major, &minor, &macro);
    return MAKEHOSVERSION(major, minor, macro);
}

auto App::IsVersionNewer(const char* current, const char* new_version) -> u32 {
    return GetVersionFromString(current) < GetVersionFromString(new_version);
}

void App::createFramebufferResources() {
    this->swapchain = nullptr;

    // Create layout for the depth buffer
    dk::ImageLayout layout_depthbuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(s_width, s_height)
        .initialize(layout_depthbuffer);

    // Create the depth buffer
    this->depthBuffer_mem = this->pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
    this->depthBuffer.initialize(layout_depthbuffer, this->depthBuffer_mem.getMemBlock(), this->depthBuffer_mem.getOffset());

    // Create layout for the framebuffers
    dk::ImageLayout layout_framebuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(s_width, s_height)
        .initialize(layout_framebuffer);

    // Create the framebuffers
    std::array<DkImage const*, NumFramebuffers> fb_array;
    const u64 fb_size  = layout_framebuffer.getSize();
    const uint32_t fb_align = layout_framebuffer.getAlignment();
    for (unsigned i = 0; i < fb_array.size(); i++) {
        // Allocate a framebuffer
        this->framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
        this->framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

        // Generate a command list that binds it
        dk::ImageView colorTarget{ framebuffers[i] }, depthTarget{ depthBuffer };
        this->cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
        this->framebuffer_cmdlists[i] = cmdbuf.finishList();

        // Fill in the array for use later by the swapchain creation code
        fb_array[i] = &framebuffers[i];
    }

    // Create the swapchain using the framebuffers
    this->swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create();

    // Generate the main rendering cmdlist
    this->recordStaticCommands();
}

void App::destroyFramebufferResources() {
    // Return early if we have nothing to destroy
    if (!this->swapchain) {
        return;
    }

    this->queue.waitIdle();
    this->cmdbuf.clear();
    swapchain.destroy();

    // Destroy the framebuffers
    for (unsigned i = 0; i < NumFramebuffers; i++) {
        framebuffers_mem[i].destroy();
    }

    // Destroy the depth buffer
    this->depthBuffer_mem.destroy();
}

void App::recordStaticCommands() {
    // Initialize state structs with deko3d defaults
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState;
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;

    // Configure the viewport and scissor
    this->cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)s_width, (float)s_height, 0.0f, 1.0f } });
    this->cmdbuf.setScissors(0, { { 0, 0, (u32)s_width, (u32)s_height } });

    // Clear the color and depth buffers
    this->cmdbuf.clearColor(0, DkColorMask_RGBA, 0.2f, 0.3f, 0.3f, 1.0f);
    this->cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

    // Bind required state
    this->cmdbuf.bindRasterizerState(rasterizerState);
    this->cmdbuf.bindColorState(colorState);
    this->cmdbuf.bindColorWriteState(colorWriteState);

    this->render_cmdlist = this->cmdbuf.finishList();
}

} // namespace sphaira
