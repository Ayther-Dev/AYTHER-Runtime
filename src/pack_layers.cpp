#include "pack_layers.h"

namespace ayther_runtime {

size_t build_pack_acetato_stack(
    const std::vector<ayther::AytherSession::PackAcetato>& acetatos,
    AytherLayerStack&                              stack) {
    size_t added = 0;
    for (const ayther::AytherSession::PackAcetato& a : acetatos) {
        // Una entrada sin lámina es una ranura de orden (mismo criterio que el
        // Lab): entra igual, para que el orden relativo de las que sí dibujan
        // sea el del pack.
        const uint32_t id =
            stack.insert_custom(a.name.c_str(), stack.layers().size());
        if (!id) continue;
        stack.set_visible(id, a.visible);
        stack.set_content(id, a.content);
        ++added;
    }
    return added;
}

}  // namespace ayther_runtime
