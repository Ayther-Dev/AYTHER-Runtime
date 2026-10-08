// ---------------------------------------------------------------------------
// pack_layers_test.cpp — #561: Runtime builds the pack overlay layer stack.
//
// The original defect was an integration omission: the session loaded pack
// overlays, but Runtime never appended them to the layer stack used by Play.
// These CPU-only tests lock down that integration contract, including draw
// order and a byte-for-byte comparison with the authoring path.
// ---------------------------------------------------------------------------
#include "pack_layers.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

std::size_t custom_layer_count(const AytherLayerStack &layer_stack) {
    std::size_t layer_count{};
    for (const AytherLayer &layer : layer_stack.layers()) {
        if (layer.kind == AytherLayerKind::Custom) {
            ++layer_count;
        }
    }
    return layer_count;
}

/// Returns custom layers in back-to-front draw order.
std::vector<const AytherLayer *> custom_layers(const AytherLayerStack &layer_stack) {
    std::vector<const AytherLayer *> result;
    for (const AytherLayer &layer : layer_stack.layers()) {
        if (layer.kind == AytherLayerKind::Custom) {
            result.push_back(&layer);
        }
    }
    return result;
}

ayther::AytherSession::PackOverlay make_overlay(const char *name, const char *asset,
                                                float parallax_factor, std::int16_t y_offset) {
    ayther::AytherSession::PackOverlay overlay;
    overlay.name = name;
    std::snprintf(overlay.content.asset, sizeof(overlay.content.asset), "%s", asset);
    overlay.content.img_w = 320;
    overlay.content.img_h = 224;
    overlay.content.y = y_offset;
    overlay.content.factor = parallax_factor;
    return overlay;
}

} // namespace

int main() {
    std::printf("== pack_layers_test (#561) ==\n");

    int passed_checks{};
    int failed_checks{};
    const auto check = [&](bool condition, const char *description) {
        if (condition) {
            ++passed_checks;
        } else {
            ++failed_checks;
        }
        std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
    };

    // A default stack proves the original defect would remain observable.
    {
        AytherLayerStack default_stack;
        check(custom_layer_count(default_stack) == 0,
              "control: the default stack contains no custom layers");
    }

    // A pack without overlays must leave the default stack unchanged.
    {
        AytherLayerStack default_stack;
        AytherLayerStack built_stack;
        const std::size_t appended_count =
            ayther_runtime::build_pack_overlay_stack({}, built_stack);
        check(appended_count == 0, "an empty overlay list appends no layers");

        bool stacks_match = default_stack.layers().size() == built_stack.layers().size();
        for (std::size_t layer_index = 0;
             stacks_match && layer_index < default_stack.layers().size(); ++layer_index) {
            const AytherLayer &default_layer = default_stack.layers()[layer_index];
            const AytherLayer &built_layer = built_stack.layers()[layer_index];
            stacks_match = default_layer.kind == built_layer.kind &&
                           default_layer.visible == built_layer.visible;
        }
        check(stacks_match, "an empty overlay list preserves the default layer stack");
    }

    // Two parallax overlays preserve their content and back-to-front order.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_overlay("sky", "sky.png", 0.25F, 0));
        overlays.push_back(make_overlay("clouds", "cloud.png", 0.50F, 16));
        overlays[1].content.blend = 1;
        overlays[1].content.opacity = 0.75F;
        overlays[1].content.fit = 1;
        overlays[1].content.flicker_amp = 0.2F;
        overlays[1].content.flicker_ticks = 4;
        overlays[1].content.tile_mode = 1;
        overlays[1].content.drift_x = 6.0F;
        overlays[1].content.pal_line = 2;
        (void)overlays[1].content.add_screen(0xC0FFEEULL);

        AytherLayerStack overlay_stack;
        const std::size_t appended_count =
            ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        check(appended_count == 2, "two overlays append two layers");
        check(custom_layer_count(overlay_stack) == 2, "the stack contains two custom layers");

        const auto overlay_layers = custom_layers(overlay_stack);
        check(overlay_layers.size() == 2 &&
                  std::strcmp(overlay_layers[0]->content.asset, "sky.png") == 0 &&
                  std::strcmp(overlay_layers[1]->content.asset, "cloud.png") == 0,
              "pack index order is preserved from back to front");

        const auto &all_layers = overlay_stack.layers();
        check(all_layers[all_layers.size() - 1].kind == AytherLayerKind::Custom &&
                  all_layers[all_layers.size() - 2].kind == AytherLayerKind::Custom,
              "pack overlays are appended to the front of the draw stack");

        const AytherLayerContent &cloud_content = overlay_layers[1]->content;
        check(cloud_content.factor == 0.50F && cloud_content.y == 16 &&
                  cloud_content.img_w == 320 && cloud_content.img_h == 224,
              "parallax, position, and dimensions are preserved");
        check(cloud_content.blend == 1 && cloud_content.opacity == 0.75F && cloud_content.fit == 1,
              "blend mode, opacity, and fit are preserved");
        check(cloud_content.flicker_amp == 0.2F && cloud_content.flicker_ticks == 4 &&
                  cloud_content.tile_mode == 1 && cloud_content.drift_x == 6.0F,
              "flicker, tiling, and drift are preserved");
        check(cloud_content.pal_line == 2 && cloud_content.gated() &&
                  cloud_content.has_screen(0xC0FFEEULL),
              "palette tint and screen gating are preserved");
        check(std::strcmp(overlay_layers[0]->name, "sky") == 0 &&
                  std::strcmp(overlay_layers[1]->name, "clouds") == 0,
              "overlay names are copied into their layers");
    }

    // Runtime and the authoring path must produce identical custom layers.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_overlay("sky", "sky.png", 0.25F, 0));
        overlays.push_back(make_overlay("clouds", "cloud.png", 0.50F, 16));
        overlays[1].content.blend = 2;
        overlays[1].content.opacity = 0.4F;
        overlays[1].content.anim_count = 1;
        overlays[1].content.anim_ticks = 6;
        std::snprintf(overlays[1].content.anim[0], sizeof(overlays[1].content.anim[0]), "%s",
                      "cloud_b.png");

        AytherLayerStack runtime_stack;
        ayther_runtime::build_pack_overlay_stack(overlays, runtime_stack);

        AytherLayerStack authoring_stack;
        for (const auto &overlay : overlays) {
            const std::uint32_t layer_id = authoring_stack.insert_custom(
                overlay.name.c_str(), authoring_stack.layers().size());
            (void)authoring_stack.set_visible(layer_id, overlay.visible);
            (void)authoring_stack.set_content(layer_id, overlay.content);
        }

        const auto runtime_layers = custom_layers(runtime_stack);
        const auto authoring_layers = custom_layers(authoring_stack);
        bool stacks_match = runtime_layers.size() == authoring_layers.size() &&
                            runtime_stack.layers().size() == authoring_stack.layers().size();
        for (std::size_t layer_index = 0; stacks_match && layer_index < runtime_layers.size();
             ++layer_index) {
            stacks_match =
                runtime_layers[layer_index]->visible == authoring_layers[layer_index]->visible &&
                std::strcmp(runtime_layers[layer_index]->name,
                            authoring_layers[layer_index]->name) == 0 &&
                std::memcmp(&runtime_layers[layer_index]->content,
                            &authoring_layers[layer_index]->content,
                            sizeof(AytherLayerContent)) == 0;
        }
        check(stacks_match, "Runtime matches the authoring layer stack byte for byte");
        check(runtime_layers.size() == 2 && runtime_layers[1]->content.anim_count == 1 &&
                  runtime_layers[1]->content.blend == 2,
              "the comparison covers populated overlay content");
    }

    // Hidden overlays remain present but are marked invisible for the renderer.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_overlay("hidden", "off.png", 0.5F, 0));
        overlays[0].visible = false;
        overlays.push_back(make_overlay("visible", "on.png", 0.5F, 0));

        AytherLayerStack overlay_stack;
        ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        const auto overlay_layers = custom_layers(overlay_stack);
        check(overlay_layers.size() == 2 && !overlay_layers[0]->visible &&
                  overlay_layers[1]->visible,
              "overlay visibility is preserved in the layer stack");
    }

    // An empty asset slot must not disturb the relative overlay order.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_overlay("background", "back.png", 0.25F, 0));
        overlays.push_back(make_overlay("empty-slot", "", 0.50F, 0));
        overlays.push_back(make_overlay("foreground", "front.png", 0.75F, 0));

        AytherLayerStack overlay_stack;
        ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        const auto overlay_layers = custom_layers(overlay_stack);
        check(overlay_layers.size() == 3 &&
                  std::strcmp(overlay_layers[0]->content.asset, "back.png") == 0 &&
                  overlay_layers[1]->content.asset[0] == 0 &&
                  std::strcmp(overlay_layers[2]->content.asset, "front.png") == 0,
              "an empty asset slot preserves its relative order");
    }

    // Spec 002, F-1b (DI-18): each overlay sits at its authored stack index,
    // interleaved with the default layers, as in the authoring workspace.
    // The pack lists the clouds at 1 and 2 (behind plane A, which carries the
    // title) and the paper and frame at 16 and 17 (on top of everything).
    // A stack is described back to front: default layers by kind, custom
    // layers by name.
    struct Slot {
        AytherLayerKind kind;
        const char *custom_name;
    };
    const auto stack_matches = [](const AytherLayerStack &layer_stack,
                                  const std::vector<Slot> &expected) {
        const std::vector<AytherLayer> &layers = layer_stack.layers();
        if (layers.size() != expected.size()) {
            return false;
        }
        for (std::size_t position = 0; position < layers.size(); ++position) {
            const AytherLayer &layer = layers[position];
            const Slot &slot = expected[position];
            if (layer.kind != slot.kind) {
                return false;
            }
            if (slot.custom_name != nullptr && std::strcmp(layer.name, slot.custom_name) != 0) {
                return false;
            }
        }
        return true;
    };
    const auto print_stack = [](const AytherLayerStack &layer_stack) {
        std::size_t position{};
        for (const AytherLayer &layer : layer_stack.layers()) {
            std::printf("    %2zu %s%s\n", position++,
                        layer.kind == AytherLayerKind::Custom ? "* " : "  ", layer.name);
        }
    };
    using K = AytherLayerKind;
    const std::vector<Slot> indexed_expected = {
        {K::VdpPlaneB, nullptr},    {K::Custom, "Nubes"},       {K::Custom, "Nubes rapidas"},
        {K::VdpPlaneA, nullptr},    {K::VdpWindow, nullptr},    {K::VdpSprites, nullptr},
        {K::TileSubs, nullptr},     {K::Video, nullptr},        {K::Picture, nullptr},
        {K::Panorama, nullptr},     {K::PlaneTilesLo, nullptr}, {K::SpritesHd, nullptr},
        {K::Mode3, nullptr},        {K::Anim, nullptr},         {K::VdpFrente, nullptr},
        {K::PlaneTilesHi, nullptr}, {K::Custom, "Papel viejo"}, {K::Custom, "Marco"},
    };
    const auto make_indexed = [](const char *name, std::uint32_t index) {
        ayther::AytherSession::PackOverlay overlay = make_overlay(name, "layer.png", 0.5F, 0);
        overlay.index = index;
        return overlay;
    };
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_indexed("Nubes", 1));
        overlays.push_back(make_indexed("Nubes rapidas", 2));
        overlays.push_back(make_indexed("Papel viejo", 16));
        overlays.push_back(make_indexed("Marco", 17));

        AytherLayerStack overlay_stack;
        const std::size_t placed_count =
            ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        check(placed_count == 4, "four indexed overlays place four layers");

        print_stack(overlay_stack);
        check(stack_matches(overlay_stack, indexed_expected),
              "F-1b: indices 1, 2, 16, 17 interleave with the default stack");
        const std::vector<AytherLayer> &layers = overlay_stack.layers();
        check(layers.size() > 3 && layers[0].kind == K::VdpPlaneB &&
                  std::strcmp(layers[1].name, "Nubes") == 0 &&
                  std::strcmp(layers[2].name, "Nubes rapidas") == 0 &&
                  layers[3].kind == K::VdpPlaneA,
              "F-1b: the clouds sit between plane B and plane A (the title)");
    }

    // The same pack listed out of index order yields the same stack.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_indexed("Marco", 17));
        overlays.push_back(make_indexed("Nubes", 1));
        overlays.push_back(make_indexed("Papel viejo", 16));
        overlays.push_back(make_indexed("Nubes rapidas", 2));

        AytherLayerStack overlay_stack;
        ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        check(stack_matches(overlay_stack, indexed_expected),
              "F-1b: placement follows index order, not pack order");
    }

    // An index beyond the stack clamps to the top; overlays without an index
    // (older packs) go on top in pack order, after the indexed ones.
    {
        std::vector<ayther::AytherSession::PackOverlay> overlays;
        overlays.push_back(make_overlay("legacy-a", "a.png", 0.5F, 0));
        overlays.push_back(make_indexed("far", 500));
        overlays.push_back(make_overlay("legacy-b", "b.png", 0.5F, 0));
        overlays.push_back(make_indexed("behind", 0));

        AytherLayerStack overlay_stack;
        ayther_runtime::build_pack_overlay_stack(overlays, overlay_stack);
        print_stack(overlay_stack);
        const std::vector<AytherLayer> &layers = overlay_stack.layers();
        check(layers.size() == 18 && std::strcmp(layers[0].name, "behind") == 0 &&
                  layers[1].kind == K::VdpPlaneB && std::strcmp(layers[15].name, "far") == 0 &&
                  std::strcmp(layers[16].name, "legacy-a") == 0 &&
                  std::strcmp(layers[17].name, "legacy-b") == 0,
              "F-1b: out-of-range indices clamp; unindexed overlays append in "
              "pack order");
    }

    std::printf("== %d OK, %d FAIL ==\n", passed_checks, failed_checks);
    return failed_checks == 0 ? 0 : 1;
}
