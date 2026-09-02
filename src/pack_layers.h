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
// ORDERING CONTRACT: baking renumbers project indices to 0..N-1 from back to
// front, and the session reader preserves that sequence. Appending each layer
// retains relative order and places overlays in front of HD lanes, matching
// the authoring frontend's `insert_custom` behavior.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <vector>

#include <ayther/ayther_layers.h>
#include <ayther/ayther_session.h>

namespace ayther_runtime {

/// Append one custom layer for each pack overlay, preserving input order.
///
/// Existing layers are never modified. An empty input leaves `layer_stack`
/// unchanged, preserving the renderer's no-overlay behavior.
/// @return The number of layers appended.
std::size_t build_pack_overlay_stack(
    const std::vector<ayther::AytherSession::PackOverlay>& overlays,
    AytherLayerStack& layer_stack);

}  // namespace ayther_runtime
