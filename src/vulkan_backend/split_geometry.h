#pragma once
// ---------------------------------------------------------------------------
// split_geometry.h — split-comparison geometry (#298).
//
// Geometry is isolated from vk_present.cpp because alignment, orientation, and
// zero-area errors can be verified without Vulkan. A zero-area blit would be a
// Vulkan validation error and can otherwise surface only as a black frame.
//
// This is pure integer geometry: no Vulkan, window, or swapchain dependency.
// ---------------------------------------------------------------------------
#include <cstdint>

namespace ayther {

/// Source and destination rectangles for one split-view blit.
struct SplitRegion {
    int32_t sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;   ///< Source-image bounds.
    int32_t dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;   ///< Swapchain bounds.
    /// Whether this region has positive source and destination area.
    /// A divider on an edge intentionally disables one half rather than
    /// emitting a zero-area Vulkan blit.
    bool valid = false;
};

/// Calculate one half of a cropped split comparison.
///
/// `from` and `to` select a normalized subrange. Values are clamped at the
/// outer boundaries, while an empty or reversed range produces an invalid
/// result. `fit_*` describes the destination of the complete unsplit image.
/// Halves are cropped, never independently scaled, preserving pixel position.
/// @return A region with `valid == false` when any extent or selected area is
///         non-positive.
inline SplitRegion split_region(int32_t src_w, int32_t src_h,
                                int32_t fit_x, int32_t fit_y,
                                int32_t fit_w, int32_t fit_h,
                                float from, float to, bool vertical) {
    SplitRegion r;
    if (from < 0.0f) from = 0.0f;
    if (to   > 1.0f) to   = 1.0f;
    if (to <= from || src_w <= 0 || src_h <= 0 || fit_w <= 0 || fit_h <= 0) return r;

    if (!vertical) {
        const int32_t s0 = static_cast<int32_t>(src_w * from);
        const int32_t s1 = static_cast<int32_t>(src_w * to);
        const int32_t d0 = fit_x + static_cast<int32_t>(fit_w * from);
        const int32_t d1 = fit_x + static_cast<int32_t>(fit_w * to);
        if (s1 <= s0 || d1 <= d0) return r;
        r = { s0, 0, s1, src_h, d0, fit_y, d1, fit_y + fit_h, true };
    } else {
        const int32_t s0 = static_cast<int32_t>(src_h * from);
        const int32_t s1 = static_cast<int32_t>(src_h * to);
        const int32_t d0 = fit_y + static_cast<int32_t>(fit_h * from);
        const int32_t d1 = fit_y + static_cast<int32_t>(fit_h * to);
        if (s1 <= s0 || d1 <= d0) return r;
        r = { 0, s0, src_w, s1, fit_x, d0, fit_x + fit_w, d1, true };
    }
    return r;
}

}  // namespace ayther
