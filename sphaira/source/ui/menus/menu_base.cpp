#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

namespace sphaira::ui::menu {

namespace { PolledData cached_data; }

auto MenuBase::GetCachedPolledData() -> PolledData { return cached_data; }

auto MenuBase::GetPolledData(bool force_refresh) -> PolledData {
    auto& data = cached_data;
    static TimeStamp timestamp{};
    static bool has_init = false;

    if (!has_init) {
        has_init = true;
        force_refresh = true;
    }

    // update every second, do this in Draw because Update() isn't called if it
    // doesn't have focus.
    if (force_refresh || timestamp.GetSeconds() >= 1) {
        data.tm = {};
        data.type = {};
        data.status = {};
        data.strength = {};
        data.ip = {};
        // avoid divide by zero if getting the size fails, for whatever reason.
        data.sd_free = 1;
        data.sd_total = 1;
        data.emmc_free = 1;
        data.emmc_total = 1;

        const auto t = std::time(NULL);
        localtime_r(&t, &data.tm);
        nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        nifmGetCurrentIpAddress(&data.ip);

        data.sd_size_known = R_SUCCEEDED(App::GetSdSize(&data.sd_free, &data.sd_total));
        data.emmc_size_known = R_SUCCEEDED(App::GetEmmcSize(&data.emmc_free, &data.emmc_total));

        timestamp.Update();
    }

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetUiButtonPos({1220, 690});
    SetAction(Button::START, Action{App::ExitToHomebrew});
}

MenuBase::~MenuBase() {
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);
    Widget::Draw(vg, theme);

    const auto pdata = GetPolledData();

    const float start_y = 70;
    const float font_size = 20;
    const float spacing = 30;

    float start_x = 1220;
    float bounds[4];

    nvgFontSize(vg, font_size);

    constexpr float bar_width = 180;
    const auto storage_y = start_y - 30;
    auto storage_x = start_x - bar_width;
    const auto storage_bar = [&](const char* name, bool sd, s64 free_bytes, s64 total_bytes, bool known) {
        const nxloader::StorageSpace space{static_cast<u64>(std::max<s64>(0, free_bytes)),
            static_cast<u64>(std::max<s64>(0, total_bytes)), known};
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto muted = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto accent = theme->GetColour(ThemeEntryID_PROGRESSBAR);
        const auto background = theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND);
        const bool preview = m_storage_preview && m_storage_preview->sd == sd;
        nxloader::CapacityEstimate estimate;
        if (preview) estimate = m_storage_preview->totals;
        const auto plan = nxloader::EvaluateCapacity(space, estimate);
        std::string label{name};
        if (!known) label += " space unavailable";
        else if (!preview) label += " " + utils::formatSizeStorage(free_bytes) + " free";
        else if (plan.calculated && !plan.can_attempt) label += " OVER CAPACITY";
        else if (!plan.calculated) label += " estimate unavailable";
        else label += std::string(plan.known ? " ~" : " <=") + utils::formatSizeStorage(plan.remaining_bytes) + " left";
        gfx::drawText(vg, storage_x, storage_y, 15, text, label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        gfx::drawRect(vg, storage_x, storage_y + 24, bar_width, 8, preview ? accent : muted, 2);
        gfx::drawRect(vg, storage_x + 1, storage_y + 25, bar_width - 2, 6, background);
        if (known && total_bytes > 0) {
            auto segments = nxloader::StorageBarSegments(space, estimate, plan);
            if (!preview) segments = {double(total_bytes - free_bytes) / total_bytes, 0, 0, double(free_bytes) / total_bytes};
            float x = storage_x + 2;
            const auto segment = [&](double fraction, NVGcolor colour) {
                const auto width = std::clamp(float(fraction) * (bar_width - 4), 0.f, storage_x + bar_width - 2 - x);
                if (width > 0) gfx::drawRect(vg, x, storage_y + 26, width, 4, colour);
                x += width;
            };
            segment(segments.used, muted);
            segment(segments.selected, plan.calculated && !plan.can_attempt ? theme->GetColour(ThemeEntryID_ERROR) : accent);
            segment(segments.reserve, nvgRGB(222, 166, 62));
        }
        if (preview && estimate.unknown_count) {
            const auto status = m_storage_preview->pending ? "Calculating - remaining space incomplete"
                : "Unknown sizes - remaining space may be lower";
            gfx::drawText(vg, storage_x, storage_y - 16, 10, muted, status);
        }
    };
    storage_bar("System", false, pdata.emmc_free, pdata.emmc_total, pdata.emmc_size_known);
    storage_x -= bar_width + spacing;
    storage_bar("microSD", true, pdata.sd_free, pdata.sd_total, pdata.sd_size_known);
    start_x -= (bar_width + spacing) * 2;

    const auto status_right = start_x;
    auto status_content_left = status_right;
    const auto draw_status = [&](ThemeEntryID colour, const std::string& text, float y) {
        gfx::drawText(vg, status_right, y, font_size, theme->GetColour(colour), text.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        gfx::textBounds(vg, 0, 0, bounds, text.c_str());
        status_content_left = std::min(status_content_left, status_right - (bounds[2] - bounds[0]));
    };

    if (!App::IsApplication()) {
        draw_status(ThemeEntryID_ERROR, "[Applet Mode]"_i18n, start_y - 28.f);
    }

    if (App::GetApp()->m_show_ip_addr.Get()) {
        if (pdata.ip) {
            char ip_buf[32];
            std::snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", pdata.ip & 0xFF, (pdata.ip >> 8) & 0xFF, (pdata.ip >> 16) & 0xFF, (pdata.ip >> 24) & 0xFF);

            char type_buf[32];
            if (pdata.type == NifmInternetConnectionType_WiFi) {
                std::snprintf(type_buf, sizeof(type_buf), "Wi-Fi %.0f%%"_i18n.c_str(), ((float)pdata.strength / 3.F) * 100);
            } else if (pdata.type == NifmInternetConnectionType_Ethernet) {
                std::snprintf(type_buf, sizeof(type_buf), "Ethernet"_i18n.c_str());
            } else {
                std::snprintf(type_buf, sizeof(type_buf), "Unknown"_i18n.c_str());
            }

            draw_status(ThemeEntryID_TEXT, std::string{type_buf} + " | " + ip_buf, start_y);
        } else {
            draw_status(ThemeEntryID_TEXT, "No Internet"_i18n, start_y);
        }
    }

    gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    constexpr float header_left = 74.f;
    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto header_right = std::min<float>(text_w, status_content_left - 15.f);
    const auto header_width = std::max(0.f, header_right - header_left);

    float title_width = header_width;
    float title_sub_x = header_right;
    float title_sub_width = 0.f;
    if (!m_title_sub_heading.empty() && header_width > 0.f) {
        nvgFontSize(vg, 28.f);
        gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());
        const auto measured_title_width = std::max(0.f, bounds[2] - bounds[0]);
        const auto max_title_width = std::max(0.f, header_width * 0.55f);
        title_width = std::min(measured_title_width + 6.f, max_title_width);
        title_sub_x = header_left + title_width + 10.f;
        title_sub_width = std::max(0.f, header_right - title_sub_x);
    }

    m_scroll_title.Draw(vg, m_title != "Homebrew", header_left, start_y, title_width, 28.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title);
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, title_sub_width, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading);
    m_scroll_title_warning.Draw(vg, true, title_sub_x, start_y - 24.f, title_sub_width, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_ERROR), m_title_warning);
    m_scroll_sub_heading.Draw(vg, true, 80, 651, SCREEN_WIDTH - 160, 16, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
}

} // namespace sphaira::ui::menu
