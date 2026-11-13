#include "nxloader/install_review.hpp"
#include "nxloader/install.hpp"
#include "app.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/option_box.hpp"
#include "utils/utils.hpp"

#include <array>

namespace sphaira::nxloader_ui {
namespace {

class InstallReview final : public ui::menu::MenuBase {
public:
    explicit InstallReview(std::shared_ptr<InstallReviewData> data)
        : MenuBase{"Review installation", ui::menu::MenuFlag_None}, m_data{std::move(data)}
        , m_list{1, 8, {60, 286, 1160, 352}, {74, 286, 1126, 44}} {
        SetTitleSubHeading(m_data->sd ? "microSD" : "system storage");
        SetAction(Button::A, Action{"Choose / details", [this] { Choose(); }});
        SetAction(Button::X, Action{"Remove", [this] { Remove(); }});
        SetAction(Button::R3, Action{[this] { Remove(); }});
        SetAction(Button::L2, Action{[this] { Remove(); }});
        SetAction(Button::R2, Action{[this] { Remove(); }});
        SetAction(Button::Y, Action{"Undo", [this] { Undo(); }});
        SetAction(Button::L, Action{"Retry size", [this] { Retry(); }});
        SetAction(Button::B, Action{"Back", [this] { SetPop(); }});
        SetAction(Button::START, Action{"Homebrew", App::ExitToHomebrew});
        m_list.SetScrollBarPos(1230, 290, 342);
        Refresh();
    }

    auto GetShortTitle() const -> const char* override { return "Review"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        if (m_revision != m_data->revision) Refresh();
        MenuBase::Update(controller, touch);
        m_list.OnUpdate(controller, touch, m_index, Count(), [this](bool touched, s64 index) {
            m_index = index;
            UpdateStatus();
            if (touched) Choose();
        });
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto selected_colour = m_plan.calculated && !m_plan.can_attempt
            ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_PROGRESSBAR);
        const auto reserve_colour = nvgRGB(222, 166, 62);
        const auto used_colour = nvgRGB(119, 128, 140);
        const auto free_colour = theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND);
        m_estimate_text.Draw(vg, true, 80, 110, 1120, 24, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, m_estimate);
        m_detail_text.Draw(vg, true, 80, 143, 1120, 18, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, m_details);
        constexpr float bar_x = 80, bar_y = 178, bar_w = 1120, bar_h = 16;
        ui::gfx::drawRect(vg, bar_x, bar_y, bar_w, bar_h, free_colour);
        const auto segments = nxloader::StorageBarSegments(m_data->storage, m_totals, m_plan);
        float x = bar_x;
        const auto draw_segment = [&](double fraction, const NVGcolor& colour) {
            const auto width = std::clamp(static_cast<float>(fraction) * bar_w, 0.f, bar_x + bar_w - x);
            if (width > 0) ui::gfx::drawRect(vg, x, bar_y, width, bar_h, colour);
            x += width;
        };
        draw_segment(segments.used, used_colour);
        draw_segment(segments.selected, selected_colour);
        draw_segment(segments.reserve, reserve_colour);
        draw_segment(segments.free, free_colour);
        const auto legend = [&](float legend_x, const char* name, const NVGcolor& colour) {
            ui::gfx::drawRect(vg, legend_x, 207, 12, 12, colour);
            ui::gfx::drawText(vg, legend_x + 20, 204, 16, info, name);
        };
        legend(80, "Already used", used_colour);
        legend(300, "Selected content", selected_colour);
        legend(560, "Reserved overhead", reserve_colour);
        legend(850, m_totals.unknown_count ? "Free (upper bound)" : "Free after install", free_colour);
        m_status_text.Draw(vg, true, 80, 236, 1120, 18, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            m_plan.can_attempt ? text : theme->GetColour(ThemeEntryID_ERROR), m_status);
        ui::gfx::drawText(vg, 80, 262, 14, info,
            "X removes a package or game; Y undoes. Unknown estimates can be installed. Sizes assume new content.");
        m_list.Draw(vg, theme, Count(), [this](auto* context, auto* current_theme, const auto& bounds, s64 index) {
            const bool action = index < 2;
            const bool focused = index == m_index;
            const auto row = action ? ReviewRow{} : m_data->rows[index - 2];
            const bool heading = !action && row.heading;
            std::string label, size_text;
            bool inactive{};
            if (action) {
                label = m_actions_text[index];
                inactive = !m_plan.can_attempt || !m_included;
            } else if (heading) {
                const auto& group = m_data->groups[row.index];
                label = group.folder.substr(group.folder.find_last_of('/') + 1);
                if (label.empty()) label = group.folder;
                label += " - " + std::to_string(m_group_counts[row.index]) + " selected";
                const auto& size = m_group_sizes[row.index];
                size_text = utils::formatSizeStorage(size.source_bytes) + " / " + (size.unknown_count ? ">=" : "~")
                    + utils::formatSizeStorage(size.installed_bytes) + (size.unknown_count ? " + ?" : "");
            } else {
                const auto& package = m_data->packages[row.index];
                inactive = !package.included || package.completed;
                label = package.completed ? "[Installed] " : package.included ? "[+] " : "[Removed] ";
                label += package.root.path.substr(package.root.path.find_last_of('/') + 1);
                if (!package.issue.empty()) label += " - " + package.issue;
                else if (!package.size.known) label += " - size estimate unavailable";
                size_text = (package.size.source_known ? utils::formatSizeStorage(package.size.source_bytes) : "file ?")
                    + std::string(" / ") + (package.size.known ? "~" : ">=")
                    + utils::formatSizeStorage(package.size.installed_bytes) + (package.size.known ? "" : " + ?");
            }
            auto colour = current_theme->GetColour(inactive ? ThemeEntryID_TEXT_INFO : heading ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            if (heading) ui::gfx::drawRect(context, bounds, current_theme->GetColour(ThemeEntryID_FOCUS));
            if (focused) ui::gfx::drawRectOutline(context, current_theme, 3, bounds);
            else ui::gfx::drawRect(context, bounds.x, bounds.y + bounds.h, bounds.w, 1,
                current_theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            m_row_text.Draw(context, focused, bounds.x + (heading ? 15 : 25), bounds.y + bounds.h / 2,
                bounds.w - 50 - (size_text.empty() ? 0 : 300), action || heading ? 20 : 18,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, label);
            if (!size_text.empty()) ui::gfx::drawText(context, bounds.x + bounds.w - 15, bounds.y + bounds.h / 2,
                16, size_text.c_str(), nullptr, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, current_theme->GetColour(ThemeEntryID_TEXT_INFO));
        });
    }

private:
    s64 Count() const { return static_cast<s64>(m_data->rows.size()) + 2; }

    std::vector<std::size_t> FocusedPackages() const {
        if (m_index < 2) return {};
        const auto& row = m_data->rows[m_index - 2];
        return row.heading ? m_data->groups[row.index].packages : std::vector<std::size_t>{row.index};
    }

    void Refresh() {
        m_revision = m_data->revision;
        m_totals = m_data->Totals();
        m_included = 0;
        m_group_counts.assign(m_data->groups.size(), 0);
        m_group_sizes.assign(m_data->groups.size(), {});
        for (std::size_t i = 0; i < m_data->groups.size(); ++i)
            for (const auto index : m_data->groups[i].packages)
                if (m_data->packages[index].included && !m_data->packages[index].completed) {
                    ++m_group_counts[i]; ++m_included; m_group_sizes[i].Add(m_data->packages[index].size);
                }
        m_plan = nxloader::EvaluateCapacity(m_data->storage, m_totals);
        SetStoragePreview(m_included ? std::optional<ui::menu::StoragePreview>{{m_totals, m_data->sd, 0}} : std::nullopt);
        m_estimate = std::to_string(m_included) + " packages | Installed " + (m_totals.unknown_count ? ">=" : "~")
            + utils::formatSizeStorage(m_totals.installed_bytes);
        if (m_totals.unknown_count) m_estimate += " + " + std::to_string(m_totals.unknown_count) + " unknown";
        m_details = "Source files: " + utils::formatSizeStorage(m_totals.source_bytes)
            + "    Available: " + (m_data->storage.known ? utils::formatSizeStorage(m_data->storage.free_bytes) : "unknown");
        if (m_plan.calculated) m_details += "    Reserve: " + utils::formatSizeStorage(m_plan.reserve_bytes);
        if (!m_included) m_status = "No packages selected. Undo a removal or return to the browser.";
        else if (!m_plan.calculated) m_status = "Capacity unavailable or size overflow. L retries; X removes the highlighted item.";
        else if (!m_plan.can_attempt) m_status = utils::formatSizeStorage(m_plan.shortage_bytes) + " over available space. X removes a package or game.";
        else if (!m_plan.known) m_status = "At most " + utils::formatSizeStorage(m_plan.remaining_bytes) + " left; unknown content may need more. Installation is available.";
        else m_status = "About " + utils::formatSizeStorage(m_plan.remaining_bytes) + " remains after installation and reserve.";
        UpdateStatus();
    }

    void UpdateStatus() {
        SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(Count())
            + " | X remove package/game | L retry size | A details | Y undo");
    }

    void Remove() {
        auto indices = FocusedPackages();
        if (indices.empty()) { App::Notify("Highlight a package or game to remove."); return; }
        std::vector<std::size_t> removed;
        for (const auto index : indices) {
            if (m_data->SetIncluded(index, false)) removed.push_back(index);
        }
        if (!removed.empty()) m_undo.push_back(std::move(removed));
        Refresh();
    }

    void Undo() {
        if (m_undo.empty()) return;
        for (const auto index : m_undo.back()) m_data->SetIncluded(index, true);
        m_undo.pop_back();
        Refresh();
    }

    void Retry() {
        auto indices = FocusedPackages();
        if (indices.empty()) {
            for (std::size_t i = 0; i < m_data->packages.size(); ++i)
                if (m_data->packages[i].included && !m_data->packages[i].completed) indices.push_back(i);
        }
        for (const auto index : indices) {
            if (m_data->packages[index].root.kind == nxloader::EntryKind::Directory) {
                auto cart = m_data->cart;
                SetPop();
                ScanAndInstall(std::move(cart));
                return;
            }
        }
        RetryReviewSizes(m_data, std::move(indices));
    }

    void Choose() {
        if (m_index < 2) {
            if (!m_included || !m_plan.can_attempt) { App::Notify(m_status); return; }
            RunReviewedBatch(m_data, m_index == 1);
            return;
        }
        const auto& row = m_data->rows[m_index - 2];
        if (row.heading) {
            App::Push<ui::OptionBox>(m_data->groups[row.index].folder + " | X removes this game; L retries its sizes.", "OK");
        } else {
            const auto& package = m_data->packages[row.index];
            App::Push<ui::OptionBox>(package.root.path + " | " + (package.size.known ? "Installed size estimated." : package.size.error)
                + (package.issue.empty() ? "" : " | " + package.issue)
                + " | X removes; L retries size. Unknown estimates do not prevent an install attempt.", "OK");
        }
    }

    std::shared_ptr<InstallReviewData> m_data;
    nxloader::CapacityEstimate m_totals;
    nxloader::CapacityPlan m_plan;
    ui::List m_list;
    s64 m_index{};
    std::uint64_t m_revision{};
    std::size_t m_included{};
    std::vector<std::size_t> m_group_counts;
    std::vector<nxloader::CapacityEstimate> m_group_sizes;
    std::vector<std::vector<std::size_t>> m_undo;
    const std::array<std::string, 2> m_actions_text{
        "Install selected - stop on first error", "Install selected - continue after errors"};
    std::string m_estimate, m_details, m_status;
    ui::ScrollingText m_estimate_text, m_detail_text, m_status_text, m_row_text;
};

}

void ShowInstallReview(std::shared_ptr<InstallReviewData> data) { App::Push<InstallReview>(std::move(data)); }

}
