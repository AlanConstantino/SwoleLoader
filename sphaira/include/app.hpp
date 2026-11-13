#pragma once

#include "nanovg.h"
#include "nanovg/dk_renderer.hpp"
#include "ui/widget.hpp"
#include "ui/notification.hpp"
#include "owo.hpp"
#include "option.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "utils/audio.hpp"
#include "nxloader/battery.hpp"

#ifdef USE_NVJPG
#include <nvjpg.hpp>
#endif
#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include <utility>

namespace sphaira {

using SoundEffect = audio::SoundEffect;

struct AmsEmummcPaths {
    char file_based_path[0x80];
    char nintendo[0x80];
};

// todo: why is this global???
void DrawElement(float x, float y, float w, float h, ThemeEntryID id);
void DrawElement(const Vec4&, ThemeEntryID id);

class App {
public:
    App(const char* argv0);
    ~App();
    void Loop();

    static App* GetApp();

    static void Exit();
    static void ExitToHomebrew();
    static void ExitRestart();
    static auto GetVg() -> NVGcontext*;

    static void Push(std::unique_ptr<ui::Widget>&&);

    template<ui::DerivedFromWidget T, typename... Args>
    static void Push(Args&&... args) {
        Push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // pops all widgets above a menu
    static void PopToMenu();

    // takes a widget off the stack without destroying it, null if it isn't there.
    // must not be called from within a widget's update.
    static auto Detach(const ui::Widget* widget) -> std::unique_ptr<ui::Widget>;

    // this is thread safe
    static void Notify(const std::string& text, ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void Notify(ui::NotifEntry entry);
    static void NotifyPop(ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void NotifyClear(ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void NotifyFlashLed();

    // if R_FAILED(rc), pushes error box. returns rc passed in.
    static Result PushErrorBox(Result rc, const std::string& message);
    // same, with extra detail shown under the message, such as the path that failed.
    static Result PushErrorBox(Result rc, const std::string& message, const std::string& detail);

    static auto GetDefaultImage() -> int;
    static auto GetDefaultImageData() -> std::span<const u8>;

    // returns argv[0]
    static auto GetExePath() -> fs::FsPath;
    // returns true if we are hbmenu.
    static auto IsHbmenu() -> bool;

    static auto GetHddEnable() -> bool;
    static auto GetWriteProtect() -> bool;
    static auto GetHddShowSystemFiles() -> bool;
    static auto GetHddIgnoreHibernation() -> bool;
    static auto GetLogEnable() -> bool;
    static auto GetInstallEnable() -> bool;
    static auto GetInstallSysmmcEnable() -> bool;
    static auto GetInstallEmummcEnable() -> bool;
    static auto GetInstallSdEnable() -> bool;
    static auto GetLanguage() -> long;
    static auto GetTextScrollSpeed() -> long;

    static void SetHddEnable(bool enable);
    static void SetWriteProtect(bool enable);
    static void SetHddShowSystemFiles(bool enable);
    static void SetHddIgnoreHibernation(bool enable);
    // re-mounts every connected usb device so that changed mount options
    // (and the ntfs specific ones in particular) take effect right away.
    static void RemountHdd();
    static void SetLogEnable(bool enable);
    static void SetInstallSysmmcEnable(bool enable);
    static void SetInstallEmummcEnable(bool enable);
    static void SetInstallSdEnable(bool enable);
    static void SetInstallPrompt(bool enable);
    static void Set12HourTimeEnable(bool enable);
    static void SetLanguage(long index);
    static void SetTextScrollSpeed(long index);

    static auto Install(OwoConfig& config) -> Result;
    static auto Install(ui::ProgressBox* pbox, OwoConfig& config) -> Result;

    static void PlaySoundEffect(SoundEffect effect);

    static void DisplayAdvancedOptions(bool left_side = true);
    static void DisplayLogOptions();
    static void DisplayInstallOptions(bool left_side = true);
    static void DisplayHddOptions(bool left_side = true);

    // helper for sidebar options to toggle install on/off
    static void ShowEnableInstallPromptOption(option::OptionBool& option, bool& enable);
    // displays an option box to enable installing, shows warning.
    static void ShowEnableInstallPrompt();

    void Draw();
    void Update();
    void Poll();

    // void DrawElement(float x, float y, float w, float h, ui::ThemeEntryID id);
    auto LoadElementImage(std::string_view value) -> ElementEntry;
    auto LoadElementColour(std::string_view value) -> ElementEntry;
    auto LoadElement(std::string_view data, ElementType type) -> ElementEntry;

    void LoadTheme(const ThemeMeta& meta);
    void CloseTheme();

    static Result GetSdSize(s64* free, s64* total);
    static Result GetEmmcSize(s64* free, s64* total);

    // helper that converts 1.2.3 to a u32 used for comparisons.
    static auto GetVersionFromString(const char* str) -> u32;
    static auto IsVersionNewer(const char* current, const char* new_version) -> u32;

    static auto IsApplication() -> bool {
        const auto type = appletGetAppletType();
        return type == AppletType_Application || type == AppletType_SystemApplication;
    }

    static auto IsApplet() -> bool {
        return !IsApplication();
    }

    // returns true if launched in applet mode with a title suspended in the background.
    static auto IsAppletWithSuspendedApp() -> bool {
        R_UNLESS(IsApplet(), false);
        R_TRY_RESULT(pmdmntInitialize(), false);
        ON_SCOPE_EXIT(pmdmntExit());

        u64 pid;
        return R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid));
    }

    static auto IsEmummc() -> bool;
    static auto IsParitionBaseEmummc() -> bool;
    static auto IsFileBaseEmummc() -> bool;

    static auto AcquireScreenToggle() -> bool;
    static void ReleaseScreenToggle();
    static auto IsScreenToggleEnabled() -> bool;
    static auto IsScreenDisabled() -> bool;
    static void SetScreenDisabled(bool disable);
    // cheap enough to call every frame: only asks the system while the screen
    // is believed to be off, so waking it some other way is still noticed.
    static auto IsScreenOff() -> bool;
    // when the last button press or touch was, for turning the screen off.
    static auto GetLastInputNs() -> u64;
    // 0 when the screen should stay on during long tasks.
    static auto GetAutoScreenOffSeconds() -> u64;
    // thread safe.
    static auto GetBattery() -> nxloader::Battery;

    static void SetAutoSleepDisabled(bool enable) {
        static Mutex mutex{};
        static int ref_count{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            appletSetAutoSleepDisabled(true);
            ref_count++;
        } else {
            if (ref_count) {
                ref_count--;
            }

            if (!ref_count) {
                appletSetAutoSleepDisabled(false);
            }
        }
    }

    static void SetBoostMode(bool enable, bool force = false) {
        static Mutex mutex{};
        static int ref_count{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            ref_count++;
            appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        } else {
            if (ref_count) {
                ref_count--;
            }
        }

        if (!ref_count || force) {
            ref_count = 0;
            appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        }
    }

    static auto GetAccountList() -> std::vector<AccountProfileBase> {
        std::vector<AccountProfileBase> out;

        AccountUid uids[ACC_USER_LIST_SIZE];
        s32 account_count;
        if (R_SUCCEEDED(accountListAllUsers(uids, std::size(uids), &account_count))) {
            for (s32 i = 0; i < account_count; i++) {
                AccountProfile profile;
                if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
                    ON_SCOPE_EXIT(accountProfileClose(&profile));

                    AccountProfileBase base;
                    if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                        // sometimes the uid for the acc can differ to the base.
                        base.uid = uids[i];
                        log_write("[ACC] found uid: 0x%016lX%016lX\n", uids[i].uid[0], uids[i].uid[1]);
                        log_write("[ACC] base  uid: 0x%016lX%016lX\n", base.uid.uid[0], base.uid.uid[1]);
                        out.emplace_back(base);
                    }
                }
            }
        }

        return out;
    }

// private:
    static constexpr inline auto CONFIG_PATH = APP_CONFIG_DIR "/config.ini";
    static constexpr inline auto INI_SECTION = "config";
    static constexpr inline auto DEFAULT_THEME_PATH = "romfs:/themes/default_theme.ini";

    fs::FsPath m_app_path;
    u64 m_start_timestamp{};
    u64 m_last_input_ns{};
    int m_default_image{};

    NVGcontext* vg{};
    PadState m_pad{};
    TouchInfo m_touch_info{};
    Controller m_controller{};
    KeyboardState m_keyboard{};

    Vec2 m_scale{1, 1};

    std::vector<std::unique_ptr<ui::Widget>> m_widgets;
    u32 m_pop_count{};
    ui::NotifMananger m_notif_manager{};

    AppletHookCookie m_appletHookCookie{};

    Theme m_theme{};

    AmsEmummcPaths m_emummc_paths{};
    bool m_quit{};

    // usb drives
    option::OptionBool m_hdd_enabled{INI_SECTION, "hdd_enabled", true};
    option::OptionBool m_hdd_write_protect{INI_SECTION, "hdd_write_protect", true};
    // ntfs only, see App::GetHddMountFlags().
    option::OptionBool m_hdd_show_system_files{INI_SECTION, "hdd_show_system_files", false};
    option::OptionBool m_hdd_ignore_hibernation{INI_SECTION, "hdd_ignore_hibernation", false};

    option::OptionBool m_log_enabled{INI_SECTION, "log_enabled", true};
    option::OptionBool m_show_ip_addr{INI_SECTION, "show_ip_addr", true};
    option::OptionLong m_language{INI_SECTION, "language", 0}; // auto
    option::OptionBool m_progress_boost_mode{INI_SECTION, "progress_boost_mode", true};
    // boost only while compressed content (NCZ) is unpacked; other transfers
    // are limited by the drive, where the faster cpu only costs battery.
    option::OptionBool m_boost_compressed_only{INI_SECTION, "boost_compressed_only", true};
    // index into the choices in DisplayAdvancedOptions, 1 minute by default.
    option::OptionLong m_auto_screen_off{INI_SECTION, "auto_screen_off", 2};
    option::OptionBool m_battery_guard{INI_SECTION, "battery_guard", true};

    // install options
    option::OptionBool m_install_sysmmc{INI_SECTION, "install_sysmmc", false};
    option::OptionBool m_install_emummc{INI_SECTION, "install_emummc", false};
    option::OptionBool m_install_sd{INI_SECTION, "install_sd", true};
    option::OptionBool m_selection_details{INI_SECTION, "selection_details", false};
    option::OptionBool m_update_check_on_launch{INI_SECTION, "update_check_on_launch", true};
    option::OptionBool m_allow_downgrade{INI_SECTION, "allow_downgrade", false};
    option::OptionBool m_skip_if_already_installed{INI_SECTION, "skip_if_already_installed", true};
    option::OptionBool m_ticket_only{INI_SECTION, "ticket_only", false};
    option::OptionBool m_skip_base{INI_SECTION, "skip_base", false};
    option::OptionBool m_skip_patch{INI_SECTION, "skip_patch", false};
    option::OptionBool m_skip_addon{INI_SECTION, "skip_addon", false};
    option::OptionBool m_skip_data_patch{INI_SECTION, "skip_data_patch", false};
    option::OptionBool m_skip_ticket{INI_SECTION, "skip_ticket", false};
    option::OptionBool m_skip_nca_hash_verify{INI_SECTION, "skip_nca_hash_verify", true};
    option::OptionBool m_skip_rsa_header_fixed_key_verify{INI_SECTION, "skip_rsa_header_fixed_key_verify", true};
    option::OptionBool m_skip_rsa_npdm_fixed_key_verify{INI_SECTION, "skip_rsa_npdm_fixed_key_verify", true};
    option::OptionBool m_ignore_distribution_bit{INI_SECTION, "ignore_distribution_bit", false};
    option::OptionBool m_convert_to_common_ticket{INI_SECTION, "convert_to_common_ticket", true};
    option::OptionBool m_convert_to_standard_crypto{INI_SECTION, "convert_to_standard_crypto", false};
    option::OptionBool m_lower_master_key{INI_SECTION, "lower_master_key", false};
    option::OptionBool m_lower_system_version{INI_SECTION, "lower_system_version", true};

    // todo: move this into it's own menu
    option::OptionLong m_text_scroll_speed{"accessibility", "text_scroll_speed", 1}; // normal


    std::shared_ptr<fs::FsNativeSd> m_fs{};

#ifdef USE_NVJPG
    nj::Decoder m_decoder;
#endif

    double m_delta_time{};

    static constexpr const char* INSTALL_DEPENDS_STR =
        "Installing is disabled.\n\n"
        "Enable in the options by selecting Menu (Y) -> Advanced -> Install options -> Enable.";

private: // from nanovg decko3d example by adubbz
    static constexpr unsigned NumFramebuffers = 2;
    static constexpr unsigned StaticCmdSize = 0x1000;
    unsigned s_width{1280};
    unsigned s_height{720};
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;
    dk::UniqueCmdBuf cmdbuf;
    CMemPool::Handle depthBuffer_mem;
    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image depthBuffer;
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;
    DkCmdList render_cmdlist;
    std::optional<nvg::DkRenderer> renderer;
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
};

} // namespace sphaira
