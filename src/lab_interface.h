#pragma once
// ---------------------------------------------------------------------------
// lab_interface.h — engine ↔ Ayther Lab contract
//
// The engine holds one ILabPlugin* under #ifdef AYTHER_WITH_LAB (pointing to
// the real AytherLabPlugin) and a NullLabPlugin otherwise.  Calling code is
// identical in both configurations; the null variant is all inline no-ops.
//
// This is the repo boundary: everything above lives in ayther-engine (public),
// everything below lives in ayther-lab (private).
//
// Call sequence each game tick:
//
//   for each SDL_Event:
//       lab.handle_sdl_event(event);
//
//   lab.on_tick_begin();
//
//   // ... runner.run_frame() + tile pipeline ...
//
//   lab.on_frame(data);       // tile occurrences + resolved subs
//
//   lab.render_overlay();     // ImGui render (Lab window)
//
//   lab.on_tick_end();
// ---------------------------------------------------------------------------

#include <cstdint>
#include <SDL3/SDL_events.h>
#include <ayther/ayther_core_ffi.h>  // AytherTileOccurrence, AytherTileSub, AytherSpriteOccurrence, AytherAudioOccurrence

// ---------------------------------------------------------------------------
// LabFrameData — snapshot of one game frame passed to the Lab UI.
// ---------------------------------------------------------------------------

struct LabFrameData {
    /// Every tile occurrence from the most-recently-processed frame.
    const AytherTileOccurrence* occs;
    uint32_t                    occ_count;

    /// Resolved HD substitutions for this frame.
    const AytherTileSub*        subs;
    uint32_t                    sub_count;

    /// Total unique tiles accumulated by TileHasher since startup.
    uint32_t                    unique_tile_count;

    /// Current frame index (increments each run_frame()).
    uint64_t                    frame_index;

    // ---- Frame snapshot (v0.7.1) — used to generate tile thumbnails --------
    /// Raw pixel data from the emulator framebuffer this frame.
    /// Pointer is valid only for the duration of the on_frame() call.
    /// Null if no video frame was produced (e.g. headless mode).
    const void* snap_pixels;
    uint32_t    snap_w;
    uint32_t    snap_h;
    uint32_t    snap_pitch;   ///< Row stride in bytes.
    uint32_t    snap_fmt;     ///< RETRO_PIXEL_FORMAT_* (0=0RGB1555, 1=XRGB8888, 2=RGB565).

    // ---- Sprite catalog fields (v0.8.1) ------------------------------------
    /// Every sprite occurrence detected in the most-recently-processed frame.
    /// Pointer is valid only for the duration of the on_frame() call.
    const AytherSpriteOccurrence* sprite_occs;
    uint32_t                      sprite_occ_count;

    /// Total unique sprite patterns accumulated by SpriteHasher since startup.
    uint32_t                      unique_sprite_count;

    // ---- Audio catalog fields (v0.9.0) -------------------------------------
    /// PCM batch occurrences from the most-recently-completed tick.
    /// Sorted by total_hits descending (most-frequent SFX first).
    /// Pointer is valid only for the duration of the on_frame() call.
    const AytherAudioOccurrence*  audio_occs;
    uint32_t                      audio_occ_count;

    /// Total unique PCM batch patterns accumulated by AudioHasher since startup.
    uint32_t                      unique_audio_count;

    // ---- Performance telemetry (v0.9.7) -------------------------------------
    /// Nominal emulator FPS from retro_system_av_info.timing.fps.
    float    emu_fps;
    /// Time spent in tile hash gather this frame (ms).
    float    tile_ms;
    /// Time spent in sprite VRAM process + gather this frame (ms).
    float    sprite_ms;
    /// Time spent in audio end_tick + gather this frame (ms).
    float    audio_ms;
    /// Audio dynamic-rate-control ratio (1.0 = neutral; ~±0.5% drift correction).
    float    audio_drc_ratio;
    /// Size of the active .ay pack file in bytes (0 = no pack loaded).
    uint64_t pack_bytes;
};

// ---------------------------------------------------------------------------
// ILabPlugin — abstract plugin interface.
// ---------------------------------------------------------------------------

class ILabPlugin {
public:
    virtual ~ILabPlugin() = default;

    /// Route an SDL event to the Lab UI (call for every event in the loop).
    virtual void handle_sdl_event(const SDL_Event& /*event*/) {}

    /// Called at the start of each game tick, before run_frame().
    /// Was onFrameBegin() in v0.6.x.
    virtual void on_tick_begin() {}

    /// Called after tile substitution is resolved — passes live catalog data.
    virtual void on_frame(const LabFrameData& /*data*/) {}

    /// Called to render the Lab window (ImGui frame may be active here).
    /// Was renderOverlay() in v0.6.x.
    virtual void render_overlay() {}

    /// Called at the end of each game tick, after render_overlay().
    /// Was onFrameEnd() in v0.6.x.
    virtual void on_tick_end() {}
};

// ---------------------------------------------------------------------------
// NullLabPlugin — zero-overhead stand-in used in non-Lab (FOSS) builds.
// ---------------------------------------------------------------------------

class NullLabPlugin final : public ILabPlugin {
    // All methods are inherited as no-ops — nothing to override.
};
