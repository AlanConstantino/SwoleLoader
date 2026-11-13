#pragma once

#include <algorithm>
#include <span>
#include <vector>

namespace nxloader {

struct FooterMeasure {
    float glyph{};
    float label{};
};

struct FooterCell {
    float left{};
    float right{};
    float glyph_right{};
    float glyph_width{};
    float label_width{};
    float scale{1};
};

// Input order is right to left, matching the controller action map.
inline std::vector<FooterCell> LayoutFooter(std::span<const FooterMeasure> measures,
                                          float left, float right) {
    std::vector<FooterCell> cells;
    if (measures.empty() || right <= left) return cells;
    cells.reserve(measures.size());

    const auto available = right - left;
    float content{};
    for (const auto& measure : measures) {
        content += std::max(0.f, measure.glyph) + 8.f + std::max(0.f, measure.label);
    }
    const auto gaps = static_cast<float>(measures.size() - 1);
    const float gap = content + gaps * 34.f <= available ? 34.f : 12.f;
    const float scale = std::min(1.f, available / (content + gaps * gap));

    auto x = right;
    for (const auto& measure : measures) {
        const auto glyph = std::max(0.f, measure.glyph) * scale;
        const auto label = std::max(0.f, measure.label) * scale;
        const auto width = glyph + 8.f * scale + label;
        cells.push_back({x - width, x, x - label - 8.f * scale, glyph, label, scale});
        x -= width + gap * scale;
    }
    return cells;
}

} // namespace nxloader
