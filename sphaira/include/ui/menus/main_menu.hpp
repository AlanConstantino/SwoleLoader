#pragma once

#include "ui/widget.hpp"
#include "ui/menus/menu_base.hpp"
#include "fs.hpp"
#include "nxloader/self_update.hpp"
#include <span>

namespace sphaira::ui::menu::main {

enum class UpdateState {
    // no check has run yet.
    Idle,
    // still downloading json from github
    Pending,
    // no update available.
    None,
    // update available!
    Update,
    // there was an error whilst checking for updates.
    Error,
};

struct MainMenu final : Widget {
    MainMenu();
    ~MainMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    auto IsMenu() const -> bool override {
        return true;
    }

private:
    void CheckForUpdates(bool show_result);
    void ParseUpdateMetadata(std::span<const u8> json);
    void ShowUpdateInfo();
    void StartUpdateInstall();

private:
    std::unique_ptr<MenuBase> m_file_browser{};

    nxloader::self_update::Check m_update{};
    std::string m_update_version{};
    std::string m_update_description{};
    std::string m_update_error{};
    UpdateState m_update_state{UpdateState::Idle};
    u64 m_update_generation{};
};

} // namespace sphaira::ui::menu::main
