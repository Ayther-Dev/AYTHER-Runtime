#pragma once
// ---------------------------------------------------------------------------
// aspect_fit.h — aspect-ratio-preserving fit (pillarbox / letterbox).
//
// The Genesis displays 4:3 (320×240 logical). Without this the 4:3 frame was
// stretched to fill the window, distorting the image on 16:9 / ultrawide
// monitors. aspect_fit() returns the largest centered rect inside `dst` that
// keeps the `src` aspect ratio; the leftover margin is the (black) bars.
//
// Used by:
//   - the runtime to size the renderer's offscreen canvas to a 4:3 box,
//   - VkPresent::blit_to_swapchain (centered blit + black bars),
//   - VkPostProcess::apply (centered CRT viewport + black bars).
// ---------------------------------------------------------------------------
#include <cstdint>

/// Integer destination rectangle in pixels.
struct FitRect { int32_t x, y, w, h; };

/// Return the largest centered destination rectangle preserving source aspect.
///
/// If either extent is empty, the function returns the full destination rect;
/// callers remain responsible for deciding whether an empty source is drawable.
/// This pure helper performs no allocation and does not throw.
inline FitRect aspect_fit(uint32_t src_w, uint32_t src_h,
                          uint32_t dst_w, uint32_t dst_h) {
    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0)
        return { 0, 0, static_cast<int32_t>(dst_w), static_cast<int32_t>(dst_h) };

    const double src_a = static_cast<double>(src_w) / src_h;
    const double dst_a = static_cast<double>(dst_w) / dst_h;

    int32_t w, h;
    if (dst_a > src_a) {            // Window wider than content: pillarbox.
        h = static_cast<int32_t>(dst_h);
        w = static_cast<int32_t>(dst_h * src_a + 0.5);
    } else {                         // Window taller than content: letterbox.
        w = static_cast<int32_t>(dst_w);
        h = static_cast<int32_t>(dst_w / src_a + 0.5);
    }
    return { (static_cast<int32_t>(dst_w) - w) / 2,
             (static_cast<int32_t>(dst_h) - h) / 2, w, h };
}
