#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nxloader {

inline std::int64_t ClampListIndex(double value, std::int64_t count) {
    if (value <= 0) {
        return 0;
    }
    if (value >= static_cast<double>(count)) {
        return count;
    }
    return static_cast<std::int64_t>(value);
}

inline std::int64_t VisibleColumns(std::int64_t columns, double origin, double extent,
                                   double stride, double clip_end) {
    if (columns <= 0 || stride <= 0 || origin + extent > clip_end) {
        return 0;
    }
    return ClampListIndex(std::floor((clip_end - origin - extent) / stride) + 1, columns);
}

// Boundary-touching cells are included, matching the list's existing scissor.
// Returning false from visit stops hit-testing at the first matching cell.
template<class Visit>
std::int64_t VisitVisibleList(std::int64_t count, std::int64_t columns,
                              double origin, double extent, double stride,
                              double clip_start, double clip_end, Visit&& visit) {
    if (count <= 0 || columns <= 0 || stride <= 0 || clip_end < clip_start) {
        return 0;
    }
    const auto rows = count / columns + (count % columns != 0);
    const auto first = ClampListIndex(std::ceil((clip_start - origin - extent) / stride), rows);
    const auto end = ClampListIndex(std::floor((clip_end - origin) / stride) + 1, rows);
    std::int64_t visited{};
    for (auto row = first; row < end; ++row) {
        const auto position = origin + static_cast<double>(row) * stride;
        const auto row_start = row * columns;
        const auto row_count = std::min(columns, count - row_start);
        for (std::int64_t column = 0; column < row_count; ++column) {
            ++visited;
            if (!visit(row_start + column, position)) {
                return visited;
            }
        }
    }
    return visited;
}

}
