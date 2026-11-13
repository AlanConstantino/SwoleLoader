#include "nxloader/footer_layout.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

void check(const std::vector<nxloader::FooterMeasure>& measures, float right) {
    const auto cells = nxloader::LayoutFooter(measures, 40.f, right);
    assert(cells.size() == measures.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        assert(cell.scale > 0 && cell.scale <= 1);
        assert(cell.left >= 39.99f && cell.right <= right + 0.01f);
        assert(cell.glyph_right - cell.glyph_width >= cell.left - 0.01f);
        assert(cell.glyph_right < cell.right - cell.label_width);
        assert(cell.right > cell.left);
        if (i) {
            // Five pixels of scaled touch padding on either side cannot overlap.
            assert(cell.right + 5.f * cell.scale <= cells[i - 1].left - 5.f * cells[i - 1].scale);
        }
    }
}

int main() {
    const std::vector<nxloader::FooterMeasure> browser{
        {26, 48}, {26, 47}, {26, 75}, {26, 50}, {30, 47}, {30, 145}, {26, 153}};
    check(browser, 1220);
    const auto ordinary = nxloader::LayoutFooter(browser, 40, 1220);
    assert(ordinary.front().scale == 1);

    // Long localized labels previously pushed the left controls through the status.
    auto translated = browser;
    for (auto& measure : translated) measure.label *= 2;
    check(translated, 1220);
    assert(nxloader::LayoutFooter(translated, 40, 1220).front().scale < 1);

    // Popup/sidebar footers use a smaller right anchor.
    check({{26, 135}, {26, 130}, {26, 180}}, 620);
    check({{26, 80}}, 240);
    check({{0, 0}, {0, 0}}, 120);
    assert(nxloader::LayoutFooter({}, 40, 1220).empty());
    assert(nxloader::LayoutFooter(browser, 40, 40).empty());

    for (int count = 1; count <= 20; ++count) {
        std::vector<nxloader::FooterMeasure> hints;
        for (int i = 0; i < count; ++i) hints.push_back({float(18 + i), float(20 + 17 * i)});
        check(hints, 1220);
    }
    std::cout << "Footer layout tests passed: bounded controls, long labels, and disjoint touch targets.\n";
}
