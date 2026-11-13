#include "ui/widget.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "log.hpp"
#include "nxloader/footer_layout.hpp"

namespace sphaira::ui {

uiButton::uiButton(Button button, const std::string& button_str, const std::string& action_str)
: m_button{button}
, m_button_str{button_str}
, m_action_str{action_str} {

}

uiButton::uiButton(Button button, const std::string& action_str)
: uiButton{button, gfx::getButton(button), action_str} {

}

auto uiButton::Draw(NVGcontext* vg, Theme* theme) -> void {
    nvgSave(vg);
    nvgFontFace(vg, "Standard");
    nvgTextLetterSpacing(vg, 0);
    nvgIntersectScissor(vg, m_pos.x, m_pos.y, m_pos.w, m_pos.h);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT));
    nvgFontSize(vg, 20.f * m_font_scale);
    nvgText(vg, m_hint_pos.x, m_hint_pos.y, m_action_str.c_str(), nullptr);
    nvgFontSize(vg, 26.f * m_font_scale);
    nvgText(vg, m_button_pos.x, m_button_pos.y, m_button_str.c_str(), nullptr);
    nvgRestore(vg);
}

void Widget::Update(Controller* controller, TouchInfo* touch) {
    for (const auto& [button, action] : m_actions) {
        if ((action.m_type & ActionType::DOWN) && controller->GotDown(button)) {
            if (static_cast<u64>(button) & static_cast<u64>(Button::ANY_BUTTON)) {
                App::PlaySoundEffect(SoundEffect::Focus);
            }
            action.Invoke(true);
            break;
        }
        else if ((action.m_type & ActionType::UP) && controller->GotUp(button)) {
            action.Invoke(false);
            break;
        }
        else if ((action.m_type & ActionType::HELD) && controller->GotHeld(button)) {
            action.Invoke(true);
            break;
        }
    }

    auto draw_actions = GetUiButtons();
    for (auto& e : draw_actions) {
        if (touch->is_clicked && touch->in_range(e.GetPos())) {
            log_write("got click: %s\n", e.m_action_str.c_str());
            FireAction(e.m_button);
            break;
        }
    }
}

void Widget::Draw(NVGcontext* vg, Theme* theme) {
    auto draw_actions = GetUiButtons();

    for (auto& e : draw_actions) {
        e.Draw(vg, theme);
    }
}

auto Widget::HasAction(Button button) const -> bool {
    return m_actions.contains(button);
}

void Widget::SetAction(Button button, const Action& action) {
    m_actions.insert_or_assign(button, action);
}

void Widget::RemoveAction(Button button) {
    if (auto it = m_actions.find(button); it != m_actions.end()) {
        m_actions.erase(it);
    }
}

auto Widget::FireAction(Button b, u8 type) -> bool {
    for (const auto& [button, action] : m_actions) {
        if (button == b && (action.m_type & type)) {
            App::PlaySoundEffect(SoundEffect::Focus);
            action.Invoke(true);
            return true;
        }
    }
    return false;
}

void Widget::SetupUiButtons(uiButtons& buttons, const Vec2& button_pos) {
    auto vg = App::GetVg();
    const auto [x, y] = button_pos;
    std::vector<nxloader::FooterMeasure> measures;
    measures.reserve(buttons.size());
    nvgSave(vg);
    nvgFontFace(vg, "Standard");
    nvgTextLetterSpacing(vg, 0);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    float bounds[4]{};
    for (const auto& e : buttons) {
        nvgFontSize(vg, 20.f);
        const auto hint_advance = nvgTextBounds(vg, 0, 0, e.m_action_str.c_str(), nullptr, bounds);
        const auto hint_width = std::max(hint_advance, bounds[2] - bounds[0]);
        nvgFontSize(vg, 26.f);
        const auto glyph_advance = nvgTextBounds(vg, 0, 0, e.m_button_str.c_str(), nullptr, bounds);
        measures.push_back({std::max(glyph_advance, bounds[2] - bounds[0]), hint_width});
    }
    nvgRestore(vg);

    const auto cells = nxloader::LayoutFooter(measures, 40.f, std::min(x, SCREEN_WIDTH - 40.f));
    for (size_t i = 0; i < cells.size(); ++i) {
        auto& e = buttons[i];
        const auto& cell = cells[i];
        e.m_font_scale = cell.scale;
        e.m_hint_pos = {cell.right, y, cell.label_width, 20.f * cell.scale};
        e.m_button_pos = {cell.glyph_right, y - 4.f, cell.glyph_width, 26.f * cell.scale};
        const auto padding = 5.f * cell.scale;
        e.SetPos(cell.left - padding, y - 8.f, cell.right - cell.left + padding * 2.f,
            std::min(36.f, SCREEN_HEIGHT - (y - 8.f)));
    }
}

auto Widget::GetUiButtons(const Actions& actions, const Vec2& button_pos) -> uiButtons {
    uiButtons draw_actions;
    draw_actions.reserve(actions.size());

    const std::pair<Button, Button> swap_buttons[] = {
        {Button::L, Button::R},
        {Button::L2, Button::R2},
    };

    // build array
    for (const auto& [button, action] : actions) {
        if (action.IsHidden() || action.m_hint.empty()) {
            continue;
        }

        uiButton ui_button{button, action.m_hint};

        // a ZL with no hint of its own beside a ZR that has one steps the same
        // thing the other way, so both glyphs are drawn in front of that hint.
        if (button == Button::R2) {
            if (const auto it = actions.find(Button::L2); it != actions.end() && it->second.m_hint.empty()) {
                ui_button.m_button_str = std::string{gfx::getButton(Button::L2)} + " " + gfx::getButton(Button::R2);
            }
        }

        bool should_swap = false;
        for (auto [left, right] : swap_buttons) {
            if (button == right && draw_actions.size() && draw_actions.back().m_button == left) {
                const auto s = draw_actions.back();
                draw_actions.back().m_button = button;
                draw_actions.back().m_button_str = gfx::getButton(button);
                draw_actions.back().m_action_str = action.m_hint;
                draw_actions.emplace_back(s);
                should_swap = true;
                break;
            }
        }

        if (!should_swap) {
            draw_actions.emplace_back(ui_button);
        }
    }

    // setup positions.
    SetupUiButtons(draw_actions, button_pos);

    return draw_actions;
}

auto Widget::GetUiButtons() const -> uiButtons {
    return GetUiButtons(m_actions, m_button_pos);
}

} // namespace sphaira::ui
