#pragma once
// ---------------------------------------------------------------------------
// #561 — pack overlay-layer stack assembled by the frontend.
//
// Per #390, the session reads and exposes pack overlays but does not assemble
// the layer stack, which is frontend state. This adapter gives the Play runtime
// the same pack-authored composition semantics as Lab.
//
// The operation is kept outside main.cpp so it can be tested without a GPU or
// window, like capture and player configuration logic.
//
// ORDERING CONTRACT (spec 002, F-1b, DI-18): each overlay carries its
// authored position among all the stack's layers (`PackOverlay::index`, the
// `index` of acetatos.toml). Overlays are inserted in ascending index order
// (stable), each at `min(index, layers().size())`, so every index counts the
// layers already placed and the stack matches the authoring workspace: an
// overlay at index 1 sits between plane B and plane A. Overlays without an
// index (`PackOverlay::kAppend`, packs baked before Engine rc.16) go on top,
// in pack order, as before.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <vector>

#include <ayther/ayther_layers.h>
#include <ayther/ayther_session.h>

namespace ayther_runtime {

/// Insert one custom layer for each pack overlay at its stack index.
///
/// Existing layers are never modified. An empty input leaves `layer_stack`
/// unchanged, preserving the renderer's no-overlay behavior.
/// @return The number of layers inserted.
std::size_t
build_pack_overlay_stack(const std::vector<ayther::AytherSession::PackOverlay> &overlays,
                         AytherLayerStack &layer_stack);

} // namespace ayther_runtime
