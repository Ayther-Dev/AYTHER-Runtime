#include "pack_layers.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ayther_runtime {

std::size_t build_pack_overlay_stack(
    const std::vector<ayther::AytherSession::PackOverlay>& overlays,
    AytherLayerStack& layer_stack) {
    std::size_t appended_count = 0;
    for (const ayther::AytherSession::PackOverlay& overlay : overlays) {
        // An entry without an asset is still an ordering slot. Appending it
        // preserves the pack's relative back-to-front ordering.
        const std::uint32_t layer_id = layer_stack.insert_custom(
            overlay.name.c_str(), layer_stack.layers().size());
        if (layer_id == 0U) {
            continue;
        }
        (void)layer_stack.set_visible(layer_id, overlay.visible);
        (void)layer_stack.set_content(layer_id, overlay.content);
        ++appended_count;
    }
    return appended_count;
}

}  // namespace ayther_runtime
