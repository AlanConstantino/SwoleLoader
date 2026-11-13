#include "nxloader/visible_range.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

struct Cell {
    std::int64_t index;
    double x;
    double y;
    bool operator==(const Cell&) const = default;
};

struct Layout {
    std::int64_t count;
    std::int64_t columns;
    float x{75};
    float y{143};
    float width{100};
    float height{60};
    float pad_x{10};
    float pad_y{0};
    float left{30};
    float top{100};
    float right{1250};
    float bottom{640};
    float scroll{};
};

// Original DrawGrid traversal, including full-width horizontal clipping.
std::vector<Cell> LegacyGrid(Layout g) {
    std::vector<Cell> cells;
    float y = g.y - g.scroll;
    for (std::int64_t i = 0; i < g.count; y += g.height + g.pad_y) {
        if (y > g.bottom) {
            break;
        }
        float x = g.x;
        for (std::int64_t column = 0; i < g.count; ++column, ++i, x += g.width + g.pad_x) {
            if (column >= g.columns || x + g.width > g.right) {
                break;
            }
            if (y + g.height < g.top) {
                continue;
            }
            cells.push_back({i, x, y});
        }
    }
    return cells;
}

std::vector<Cell> VisibleGrid(Layout g, std::int64_t* visits = nullptr) {
    std::vector<Cell> cells;
    const auto columns = nxloader::VisibleColumns(g.columns, g.x, g.width, g.width + g.pad_x, g.right);
    const auto count = nxloader::VisitVisibleList(g.count, columns, double(g.y) - g.scroll,
        g.height, g.height + g.pad_y, g.top, g.bottom, [&](auto i, double y) {
            cells.push_back({i, double(g.x) + (i % columns) * double(g.width + g.pad_x), y});
            return true;
        });
    if (visits) {
        *visits = count;
    }
    return cells;
}

void CompareLayouts() {
    for (const auto count : {0, 1, 7, 8, 33, 1000}) {
        for (const auto columns : {1, 2, 3, 8, 15}) {
            for (const auto scroll : {0.f, 42.75f, 43.f, 43.25f, 60.f, 120.5f, 3000.f, 60000.f}) {
                Layout g{count, columns};
                g.scroll = scroll;
                assert(VisibleGrid(g) == LegacyGrid(g));
                g.width = 89.5;
                g.height = 59.75;
                g.pad_x = 8.25;
                g.pad_y = 4.5;
                assert(VisibleGrid(g) == LegacyGrid(g));
            }
        }
    }
}

void CompareHorizontal() {
    for (const auto count : {0, 1, 10, 1000}) {
        for (const auto scroll : {0., 100., 100.25, 500., 108900.}) {
            std::vector<Cell> old;
            std::vector<Cell> now;
            for (std::int64_t i = 0; i < count; ++i) {
                const auto x = 75. - scroll + i * 110.;
                if (x + 100. < 30.) {
                    continue;
                }
                if (x > 1250.) {
                    break;
                }
                old.push_back({i, x, 143});
            }
            nxloader::VisitVisibleList(count, 1, 75. - scroll, 100., 110., 30., 1250., [&](auto i, double x) {
                now.push_back({i, x, 143});
                return true;
            });
            assert(old == now);
        }
    }
}

void TouchMatchesDrawing() {
    Layout g{35, 3};
    g.scroll = 140.5;
    const auto cells = VisibleGrid(g);
    const auto columns = nxloader::VisibleColumns(g.columns, g.x, g.width, g.width + g.pad_x, g.right);
    for (const auto& drawn : cells) {
        const auto touch_x = drawn.x + g.width / 2;
        const auto touch_y = std::max(drawn.y, double(g.top)) +
            (std::min(drawn.y + g.height, double(g.bottom)) - std::max(drawn.y, double(g.top))) / 2;
        std::int64_t selected = -1;
        const auto visits = nxloader::VisitVisibleList(g.count, columns, double(g.y) - g.scroll,
            g.height, g.height + g.pad_y, g.top, g.bottom, [&](auto i, double y) {
                const auto x = g.x + (i % columns) * (g.width + g.pad_x);
                if (touch_x >= x && touch_x <= x + g.width && touch_y >= y && touch_y <= y + g.height) {
                    selected = i;
                    return false;
                }
                return true;
            });
        assert(selected == drawn.index);
        assert(visits <= static_cast<std::int64_t>(cells.size()));
    }
}

void LargeListsStayBounded() {
    for (const auto count : {1000LL, 100000LL, 1000000000LL}) {
        std::int64_t callbacks{};
        const auto visits = nxloader::VisitVisibleList(count, 1, 143. - (count - 12) * 60.,
            60., 60., 100., 640., [&](auto, double y) {
                assert(y + 60. >= 100. && y <= 640.);
                ++callbacks;
                return true;
            });
        assert(visits == callbacks && visits == 10);
    }
    assert(nxloader::VisibleColumns(3, 200, 100, 110, 250) == 0);
    assert(nxloader::VisibleColumns(3, 75, 100, 110, 395) == 3);
    assert(nxloader::VisibleColumns(3, 75, 100, 110, 394.75) == 2);
    const auto no_visit = [](auto, double) { assert(false); return true; };
    assert(nxloader::VisitVisibleList(10, 0, 0, 1, 1, 0, 5, no_visit) == 0);
    assert(nxloader::VisitVisibleList(10, 1, 0, 1, 0, 0, 5, no_visit) == 0);
}

int main() {
    CompareLayouts();
    CompareHorizontal();
    TouchMatchesDrawing();
    LargeListsStayBounded();
    std::cout << "List visibility tests passed (legacy layout equivalence, touch identity, bounded billion-item traversal).\n";
}
