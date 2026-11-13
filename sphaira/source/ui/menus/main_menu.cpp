#include "ui/menus/main_menu.hpp"

#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/install_queue.hpp"
#include "ui/error_box.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/nvg_util.hpp"

#include "ui/menus/filebrowser.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "nacp_compat.hpp"
#include "nro.hpp"
#include "nxloader/update_signature.hpp"

#include <cstring>
#include <yyjson.h>
#include <iomanip>
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string_view>

namespace sphaira::ui::menu::main {
namespace {

namespace self_update = nxloader::self_update;

// Public half of the key that signs releases. See RELEASING.md.
constexpr u8 UPDATE_PUBLIC_KEY[]{
    #embed <update_public_key.der>
};

constexpr s64 UPDATE_IO_CHUNK{1024 * 1024};

auto UpdatesConfigured() -> bool {
    return APP_UPDATE_REPO[0] != '\0';
}

auto Trim(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

auto Lower(std::string_view value) -> std::string {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](char c){
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

auto ReadNotes(yyjson_val* value) -> std::string {
    if (!value) {
        return {};
    }
    if (yyjson_is_str(value)) {
        const auto text = yyjson_get_str(value);
        return text ? text : "";
    }
    if (!yyjson_is_arr(value)) {
        return {};
    }

    std::string notes;
    size_t index{}, count{};
    yyjson_val* item{};
    yyjson_arr_foreach(value, index, count, item) {
        if (const auto text = yyjson_get_str(item); text && *text) {
            if (!notes.empty()) {
                notes += '\n';
            }
            notes += "- ";
            notes += text;
        }
    }
    return notes;
}

auto ReadLocalizedNotes(yyjson_val* object, std::string_view language) -> std::string {
    if (!object || !yyjson_is_obj(object)) {
        return {};
    }

    auto value = yyjson_obj_getn(object, language.data(), language.size());
    if (!value && language.find('-') != std::string_view::npos) {
        const auto base = language.substr(0, language.find('-'));
        value = yyjson_obj_getn(object, base.data(), base.size());
    }
    if (!value) {
        value = yyjson_obj_get(object, "en");
    }
    return ReadNotes(value);
}

// Supports the language-keyed changelog format used by the supplied NRO. It
// also accepts an object containing language keys directly, which makes the
// same format convenient for a GitHub release body.
auto ParseLocalizedNotesJson(std::string_view input, std::string_view version, std::string_view language) -> std::string {
    const auto document = yyjson_read(input.data(), input.size(), YYJSON_READ_NOFLAG);
    if (!document) {
        return {};
    }
    ON_SCOPE_EXIT(yyjson_doc_free(document));

    const auto root = yyjson_doc_get_root(document);
    if (!root || !yyjson_is_obj(root)) {
        return {};
    }

    const auto versions = yyjson_obj_get(root, "versions");
    if (versions && yyjson_is_arr(versions)) {
        const auto wanted = App::GetVersionFromString(std::string{version}.c_str());
        size_t index{}, count{};
        yyjson_val* entry{};
        yyjson_arr_foreach(versions, index, count, entry) {
            const auto version_value = yyjson_obj_get(entry, "version");
            const auto entry_version = version_value ? yyjson_get_str(version_value) : nullptr;
            if (!entry_version) {
                continue;
            }
            const auto parsed = App::GetVersionFromString(entry_version);
            if ((wanted && parsed == wanted) || (!wanted && version == entry_version)) {
                return ReadLocalizedNotes(entry, language);
            }
        }
        return {};
    }

    return ReadLocalizedNotes(root, language);
}

auto ParseLanguageHeading(std::string_view line) -> std::optional<std::string> {
    line = Trim(line);
    if (line.starts_with("<!--") && line.ends_with("-->")) {
        line.remove_prefix(4);
        line.remove_suffix(3);
        line = Trim(line);
        if (const auto colon = line.find(':'); colon != std::string_view::npos
            && Lower(Trim(line.substr(0, colon))) == "lang") {
            line = Trim(line.substr(colon + 1));
        } else {
            return {};
        }
    } else {
        while (!line.empty() && line.front() == '#') {
            line.remove_prefix(1);
        }
        line = Trim(line);
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            line.remove_prefix(1);
            line.remove_suffix(1);
        }
    }

    auto code = Lower(Trim(line));
    static constexpr std::array<std::string_view, 16> languages{
        "en", "ja", "fr", "de", "it", "es", "zh-cn", "ko", "nl",
        "pt", "ru", "zh-tw", "se", "vi", "uk", "tr",
    };
    if (std::ranges::find(languages, code) == languages.end()) {
        return {};
    }
    if (code == "zh-cn") return "zh-CN";
    if (code == "zh-tw") return "zh-TW";
    return code;
}

auto LocalizeReleaseNotes(std::string_view notes, std::string_view version) -> std::string {
    if (notes.empty()) {
        return "No changelog available."_i18n;
    }

    const auto language = i18n::GetLanguageCode();
    if (const auto json_notes = ParseLocalizedNotesJson(notes, version, language); !json_notes.empty()) {
        return json_notes;
    }

    // Optional Markdown format:
    //   ## [en]
    //   English notes
    //   ## [fr]
    //   French notes
    // Automatic language uses the resolved console language here.
    std::string selected;
    std::string english;
    std::string active_language;
    bool found_language_section{};
    for (std::size_t offset{}; offset <= notes.size();) {
        const auto end = notes.find('\n', offset);
        const auto line = notes.substr(offset, end == std::string_view::npos ? notes.size() - offset : end - offset);
        if (const auto heading = ParseLanguageHeading(line)) {
            active_language = *heading;
            found_language_section = true;
        } else if (!active_language.empty()) {
            auto& output = active_language == language ? selected : (active_language == "en" ? english : selected);
            if (active_language == language || active_language == "en") {
                output.append(line);
                output.push_back('\n');
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }

    if (found_language_section) {
        auto result = !selected.empty() ? std::move(selected) : std::move(english);
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
            result.pop_back();
        }
        if (!result.empty()) {
            return result;
        }
    }
    return std::string{notes};
}

class UpdateInfoMenu final : public MenuBase {
public:
    UpdateInfoMenu(std::string version, std::string notes, bool update_available, std::function<void()> on_update)
    : MenuBase{"Update Checker"_i18n, MenuFlag_None}
    , m_version{std::move(version)}
    , m_update_available{update_available}
    , m_on_update{std::move(on_update)} {
        SetAction(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }});
        if (m_update_available) {
            SetAction(Button::A, Action{"Update Now"_i18n, [this](){
                if (m_on_update) {
                    m_on_update();
                }
            }});
        }
        m_notes = std::make_unique<ScrollableText>(notes, 0.f, 205.f, 385.f, 1000.f, 20.f);
        SetTitleSubHeading(m_version.empty() ? "v" APP_DISPLAY_VERSION : m_version);
    }

    auto GetShortTitle() const -> const char* override { return "Update"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        m_notes->Update(controller, touch);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto status = m_update_available ? "New version available"_i18n : "You are already on the latest version."_i18n;
        gfx::drawText(vg, 110.f, 125.f, 25.f, theme->GetColour(ThemeEntryID_TEXT), status.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, 110.f, 166.f, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "Changelog"_i18n.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawRect(vg, 110.f, 187.f, 1040.f, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        m_notes->Draw(vg, theme);
    }

private:
    std::string m_version;
    bool m_update_available{};
    std::function<void()> m_on_update;
    std::unique_ptr<ScrollableText> m_notes;
};

// Hashes a file that must be expected_size bytes long. Stops early when the
// user cancels only if cancellable is set.
auto HashFile(ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& path, s64 expected_size, bool cancellable, self_update::Sha256& out) -> Result {
    fs::File file;
    R_TRY(fs->OpenFile(path, FsOpenMode_Read, &file));
    s64 size{};
    R_TRY(file.GetSize(&size));
    R_UNLESS(size == expected_size, Result_MainDownloadedUpdateInvalid);

    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    std::vector<u8> buffer(UPDATE_IO_CHUNK);
    for (s64 offset{}; offset < size;) {
        if (cancellable) {
            R_TRY(pbox->ShouldExitResult());
        }
        u64 bytes_read{};
        R_TRY(file.Read(offset, buffer.data(), std::min(UPDATE_IO_CHUNK, size - offset), FsReadOption_None, &bytes_read));
        R_UNLESS(bytes_read, Result_MainDownloadedUpdateInvalid);
        sha256ContextUpdate(&ctx, buffer.data(), bytes_read);
        offset += bytes_read;
        pbox->UpdateTransfer(offset, size);
    }
    sha256ContextGetHash(&ctx, out.data());
    R_SUCCEED();
}

// Copies a file and ignores cancellation, so the running app is never left
// half written.
auto CopyWithoutCancel(ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& src, const fs::FsPath& dst) -> Result {
    fs::File in;
    R_TRY(fs->OpenFile(src, FsOpenMode_Read, &in));
    s64 size{};
    R_TRY(in.GetSize(&size));

    // fails if the file exists, which is fine as it is resized below.
    fs->CreateFile(dst, size, 0);
    fs::File out;
    R_TRY(fs->OpenFile(dst, FsOpenMode_Write, &out));
    R_TRY(out.SetSize(size));

    std::vector<u8> buffer(UPDATE_IO_CHUNK);
    for (s64 offset{}; offset < size;) {
        u64 bytes_read{};
        R_TRY(in.Read(offset, buffer.data(), std::min(UPDATE_IO_CHUNK, size - offset), FsReadOption_None, &bytes_read));
        R_UNLESS(bytes_read, Result_MainFailedToInstallUpdate);
        R_TRY(out.Write(offset, buffer.data(), bytes_read, FsWriteOption_None));
        offset += bytes_read;
        pbox->UpdateTransfer(offset, size);
    }
    R_SUCCEED();
}

auto InstallUpdate(ProgressBox* pbox, const self_update::Check& update, const std::string& version) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    // Staging files sit next to the running app, wherever it is installed.
    const auto exe_path = App::GetExePath();
    const fs::FsPath staged_nro{std::string{exe_path.s} + ".update"};
    const fs::FsPath backup_nro{std::string{exe_path.s} + ".backup"};
    fs.DeleteFile(staged_nro);
    fs.DeleteFile(backup_nro);
    ON_SCOPE_EXIT(fs.DeleteFile(staged_nro));

    // 1. Download the signature and the new version. The running app is not
    // touched until the download has been verified.
    pbox->NewTransfer("Downloading signature"_i18n);
    const auto signature = curl::Api().ToMemory(
        curl::Url{update.signature.url},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    R_UNLESS(signature.success && signature.data.size() == update.signature.size, Result_MainFailedToDownloadUpdate);

    pbox->NewTransfer(i18n::Reorder("Downloading ", version));
    log_write("[UPD] downloading %s\n", update.app.url.c_str());
    const auto download = curl::Api().ToFile(
        curl::Url{update.app.url},
        curl::Path{staged_nro},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    R_UNLESS(download.success, Result_MainFailedToDownloadUpdate);

    // 2. Only install a file signed with the release key that also reports
    // the version the release promised.
    pbox->NewTransfer("Verifying update"_i18n);
    self_update::Sha256 digest{};
    R_TRY(HashFile(pbox, &fs, staged_nro, update.app.size, true, digest));
    if (!self_update::VerifySignature(UPDATE_PUBLIC_KEY, digest, signature.data)) {
        log_write("[UPD] signature check failed\n");
        R_THROW(Result_MainUpdateSignatureInvalid);
    }

    NacpStruct nacp{};
    R_UNLESS(R_SUCCEEDED(nro_get_nacp(staged_nro, nacp)), Result_MainDownloadedUpdateInvalid);
    const std::string_view staged_version{nacp.display_version, strnlen(nacp.display_version, sizeof(nacp.display_version))};
    if (self_update::ParseVersion(staged_version) != update.version) {
        log_write("[UPD] release %s contains version %.*s\n", version.c_str(), (int)staged_version.size(), staged_version.data());
        R_THROW(Result_MainDownloadedUpdateInvalid);
    }
    R_TRY(pbox->ShouldExitResult());

    // 3. Replace the running app, keeping a backup until the installed copy
    // reads back identical to the verified download.
    pbox->NewTransfer("Backing up current version"_i18n);
    R_TRY(CopyWithoutCancel(pbox, &fs, exe_path, backup_nro));

    pbox->NewTransfer("Installing update"_i18n);
    auto rc = CopyWithoutCancel(pbox, &fs, staged_nro, exe_path);
    self_update::Sha256 installed{};
    if (R_SUCCEEDED(rc)) {
        pbox->NewTransfer("Checking installed update"_i18n);
        rc = HashFile(pbox, &fs, exe_path, update.app.size, false, installed);
    }
    if (R_FAILED(rc) || installed != digest) {
        log_write("[UPD] install failed: 0x%X, restoring backup\n", R_VALUE(rc));
        pbox->NewTransfer("Restoring previous version"_i18n);
        if (const auto restore_rc = CopyWithoutCancel(pbox, &fs, backup_nro, exe_path); R_FAILED(restore_rc)) {
            // Keep the backup so it can be copied back by hand.
            log_write("[UPD] backup restore failed: 0x%X, backup kept at %s\n", R_VALUE(restore_rc), backup_nro.s);
        } else {
            fs.DeleteFile(backup_nro);
        }
        R_THROW(Result_MainFailedToInstallUpdate);
    }
    fs.DeleteFile(backup_nro);

    log_write("[UPD] installed %s to %s\n", version.c_str(), exe_path.s);
    R_SUCCEED();
}

} // namespace

MainMenu::MainMenu() {
    this->SetActions(
        std::make_pair(Button::START, Action{"Homebrew menu", App::ExitToHomebrew}),
        std::make_pair(Button::Y, Action{"Menu"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Menu Options"_i18n, "v" APP_DISPLAY_VERSION, Sidebar::Side::LEFT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            SidebarEntryArray::Items language_items;
            language_items.push_back("Auto"_i18n);
            language_items.push_back("English");
            language_items.push_back("日本語"); // Japanese
            language_items.push_back("Français"); // French
            language_items.push_back("Deutsch"); // German
            language_items.push_back("Italiano"); // Italian
            language_items.push_back("Español"); // Spanish
            language_items.push_back("简体中文"); // Chinese (Simplified)
            language_items.push_back("한국어"); // Korean
            language_items.push_back("Nederlands"); // Dutch
            language_items.push_back("Português"); // Portuguese
            language_items.push_back("Русский"); // Russian
            language_items.push_back("繁體中文"); // Chinese (Traditional)
            language_items.push_back("Svenska"); // Swedish
            language_items.push_back("Tiếng Việt"); // Vietnamese
            language_items.push_back("Українська"); // Ukrainian
            language_items.push_back("Türkçe"); // Turkish

            options->Add<SidebarEntryCallback>("Network"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Network Options"_i18n, Sidebar::Side::LEFT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("FTP"_i18n, [](){ App::DisplayFtpOptions(); },
                    i18n::get("ftp_settings_info",
                        "Enable / modify the FTP server settings such as port, user/pass and the folders that are shown.\n\n"
                        "NOTE: Changing any of the options will automatically restart the FTP server when exiting the options menu.")
                );

                options->Add<SidebarEntryCallback>("MTP"_i18n, [](){ App::DisplayMtpOptions(); },
                    i18n::get("mtp_settings_info",
                        "Enable / modify the MTP responder settings such as the folders that are shown.\n\n"
                        "NOTE: Changing any of the options will automatically restart the MTP server when exiting the options menu.")
                );

                options->Add<SidebarEntryCallback>("HDD"_i18n, [](){
                    App::DisplayHddOptions();
                },  "Enable / modify the HDD mount options."_i18n);

                options->Add<SidebarEntryBool>("NXlink"_i18n, App::GetNxlinkEnable(), [](bool& enable){
                    App::SetNxlinkEnable(enable);
                },  i18n::get("nxlink_enable_info",
                        "Enable NXlink server to run in the background. "
                        "NXlink is used to send .nro's from PC to the switch\n\n"
                        "If you are not a developer, you can disable this option."));

            },  i18n::get("network_options_info",
                    "Toggle FTP, MTP, HDD and NXlink"));

            options->Add<SidebarEntryCallback>("Theme"_i18n, [](){
                App::DisplayThemeOptions();
            }, "Customise the look of Sphaira by changing the theme"_i18n);

            options->Add<SidebarEntryArray>("Language"_i18n, language_items, [](s64& index_out){
                App::SetLanguage(index_out);
            }, (s64)App::GetLanguage(),
                i18n::get("translation_info",
                    "Change the language.\n\n"
                    "If your language isn't found, or translations are missing, please consider opening a PR at "
                    "github.com/ITotalJustice/sphaira"));

            options->Add<SidebarEntryCallback>("Advanced Options"_i18n, [](){
                App::DisplayAdvancedOptions();
            },  i18n::get("advanced_options_info",
                    "Change the advanced options. "
                    "Please view the info boxes to better understand each option."));

            if (m_update_state == UpdateState::Update) {
                options->Add<SidebarEntryCallback>("Update available: "_i18n + m_update_version, [this](){
                    ShowUpdateInfo();
                }, true, "View the changelog and install the update."_i18n);
            }

            options->Add<SidebarEntryCallback>("Check for updates"_i18n, [this](){
                CheckForUpdates(true);
            }, true, UpdatesConfigured()
                ? "Check github.com/" APP_UPDATE_REPO " for a newer version and view its changelog."_i18n
                : "Updates are not set up for this build."_i18n);

            options->Add<SidebarEntryCallback>("About " APP_NAME, [](){
                App::Push<OptionBox>(APP_NAME " " APP_VERSION " - powered by Sphaira", "OK");
            }, true);
        }}
    ));

    m_file_browser = std::make_unique<ui::menu::filebrowser::Menu>(MenuFlag_Tab);

    if (App::GetApp()->m_update_check_on_launch.Get()) {
        CheckForUpdates(false);
    }

    for (auto [button, action] : m_actions) {
        m_file_browser->SetAction(button, action);
    }
}

MainMenu::~MainMenu() {

}

void MainMenu::CheckForUpdates(bool show_result) {
    if (!UpdatesConfigured()) {
        if (show_result) {
            App::Push<OptionBox>("Updates are not set up for this build."_i18n, "OK"_i18n);
        }
        return;
    }

    const auto generation = ++m_update_generation;
    m_update_state = UpdateState::Pending;
    if (show_result) {
        App::Notify("Checking for updates"_i18n);
    }

    const auto queued = curl::Api().ToMemoryAsync(
        curl::Url{self_update::LatestReleaseUrl(APP_UPDATE_REPO)},
        curl::StopToken{this->GetToken()},
        curl::Header{
            { "Accept", "application/vnd.github+json" },
            { "X-GitHub-Api-Version", "2022-11-28" },
        },
        curl::OnComplete{[this, generation, show_result](auto& result){
            if (generation != m_update_generation) {
                return;
            }

            if (result.success) {
                ParseUpdateMetadata(result.data);
            } else {
                m_update_state = UpdateState::Error;
                if (result.code == 404) {
                    m_update_error = "No release has been published yet."_i18n;
                } else if (result.code >= 400) {
                    m_update_error = "GitHub returned an error (HTTP "_i18n + std::to_string(result.code) + "). Try again later."_i18n;
                } else {
                    m_update_error = "Could not reach GitHub. Check the internet connection and try again."_i18n;
                }
            }
            log_write("[UPD] check status: %u http: %ld\n", static_cast<u8>(m_update_state), result.code);

            if (show_result) {
                ShowUpdateInfo();
            } else if (m_update_state == UpdateState::Update) {
                App::Notify("Update available: "_i18n + m_update_version);
                App::Notify("Open Menu Options to view the changelog."_i18n);
            }
        }}
    );

    if (!queued) {
        m_update_state = UpdateState::Error;
        m_update_error = "Could not start the update check."_i18n;
        if (show_result) {
            ShowUpdateInfo();
        }
    }
}

void MainMenu::ParseUpdateMetadata(std::span<const u8> json) {
    m_update_state = UpdateState::Error;
    m_update_error = "GitHub sent a release description that could not be read."_i18n;

    auto document = yyjson_read(reinterpret_cast<const char*>(json.data()), json.size(), YYJSON_READ_NOFLAG);
    if (!document) {
        return;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(document));

    const auto root = yyjson_doc_get_root(document);
    if (!root || !yyjson_is_obj(root)) {
        return;
    }

    const auto get_str = [](yyjson_val* object, const char* key) -> std::string {
        const auto value = yyjson_obj_get(object, key);
        const auto text = value && yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
        return text ? text : "";
    };

    self_update::Release release;
    release.tag = get_str(root, "tag_name");
    release.notes = get_str(root, "body");
    if (const auto assets = yyjson_obj_get(root, "assets"); assets && yyjson_is_arr(assets)) {
        size_t index{}, count{};
        yyjson_val* asset{};
        yyjson_arr_foreach(assets, index, count, asset) {
            if (!asset || !yyjson_is_obj(asset)) {
                continue;
            }
            const auto size = yyjson_obj_get(asset, "size");
            release.assets.push_back({
                .name = get_str(asset, "name"),
                .url = get_str(asset, "browser_download_url"),
                .size = size && yyjson_is_uint(size) ? yyjson_get_uint(size) : 0,
            });
        }
    }

    const auto current = self_update::ParseVersion(APP_VERSION).value_or(0);
    m_update = self_update::Evaluate(release, current, APP_NRO_NAME);
    m_update_version = release.tag;
    m_update_description = LocalizeReleaseNotes(release.notes, release.tag);

    switch (m_update.status) {
        case self_update::Status::UpToDate:
            m_update_state = UpdateState::None;
            break;
        case self_update::Status::Available:
            m_update_state = UpdateState::Update;
            log_write("[UPD] found %s: %s\n", m_update_version.c_str(), m_update.app.url.c_str());
            break;
        case self_update::Status::Invalid:
            m_update_error = i18n::get(m_update.error);
            log_write("[UPD] release %s rejected: %.*s\n", m_update_version.c_str(), (int)m_update.error.size(), m_update.error.data());
            break;
    }
}

void MainMenu::ShowUpdateInfo() {
    if (m_update_state == UpdateState::Idle) {
        CheckForUpdates(true);
        return;
    }
    if (m_update_state == UpdateState::Pending) {
        App::Notify("Checking for updates"_i18n);
        return;
    }
    if (m_update_state == UpdateState::Error) {
        App::Push<OptionBox>("Failed to check for updates"_i18n + "\n\n" + m_update_error, "OK"_i18n);
        return;
    }

    App::Push<UpdateInfoMenu>(
        m_update_version,
        m_update_description.empty() ? "No changelog available."_i18n : m_update_description,
        m_update_state == UpdateState::Update,
        [this](){ StartUpdateInstall(); }
    );
}

void MainMenu::StartUpdateInstall() {
    if (m_update_state != UpdateState::Update) {
        return;
    }

    App::Push<ProgressBox>(0, "Downloading "_i18n, APP_NAME " " + m_update_version, [update = m_update, version = m_update_version](auto pbox) -> Result {
        return InstallUpdate(pbox, update, version);
    }, [this](Result rc){
        if (rc == Result_MainUpdateSignatureInvalid) {
            App::Push<OptionBox>(
                "The download is not signed by the developer of " APP_NAME ", so it was not installed."_i18n,
                "OK"_i18n
            );
            return;
        }
        App::PushErrorBox(rc, "Failed to install update"_i18n);
        if (R_SUCCEEDED(rc)) {
            m_update_state = UpdateState::None;
            App::Notify("Update complete. Restarting " APP_NAME "."_i18n);
            App::Push<OptionBox>(
                "Press OK to restart " APP_NAME ""_i18n, "OK"_i18n, [](auto){
                    App::ExitRestart();
                }
            );
        }
    });
}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    m_file_browser->Update(controller, touch);
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    m_file_browser->Draw(vg, theme);
}

void MainMenu::OnFocusGained() {
    Widget::OnFocusGained();
    m_file_browser->OnFocusGained();
}

void MainMenu::OnFocusLost() {
    Widget::OnFocusLost();
    m_file_browser->OnFocusLost();
}

} // namespace sphaira::ui::menu::main
