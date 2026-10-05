#include "pack_layers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ayther_runtime {

std::size_t
build_pack_overlay_stack(const std::vector<ayther::AytherSession::PackOverlay> &overlays,
                         AytherLayerStack &layer_stack) {
    // Spec 002, F-1b (DI-18): place each overlay at its authored stack index.
    // Walking them in ascending index order (stable, so equal indices keep
    // pack order) makes every index count the layers already placed, which
    // reproduces the authoring stack. Overlays without an index (kAppend,
    // older packs) sort last and are clamped to the top, in pack order.
    std::vector<const ayther::AytherSession::PackOverlay *> ordered;
    ordered.reserve(overlays.size());
    for (const ayther::AytherSession::PackOverlay &overlay : overlays) {
        ordered.push_back(&overlay);
    }
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [](const ayther::AytherSession::PackOverlay *lhs,
           const ayther::AytherSession::PackOverlay *rhs) { return lhs->index < rhs->index; });

    std::size_t placed_count = 0;
    for (const ayther::AytherSession::PackOverlay *overlay : ordered) {
        // An entry without an asset is still an ordering slot.
        const std::size_t position =
            std::min(static_cast<std::size_t>(overlay->index), layer_stack.layers().size());
        const std::uint32_t layer_id = layer_stack.insert_custom(overlay->name.c_str(), position);
        if (layer_id == 0U) {
            continue;
        }
        (void)layer_stack.set_visible(layer_id, overlay->visible);
        (void)layer_stack.set_content(layer_id, overlay->content);
        ++placed_count;
    }
    return placed_count;
}

} // namespace ayther_runtime
