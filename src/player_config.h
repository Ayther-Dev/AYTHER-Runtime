#pragma once
// ---------------------------------------------------------------------------
// player_config.h — persisted player preferences (#299).
//
// Preferences are scoped to a game-and-pack pair. Subsystems and profiles are
// pack-specific; sharing their settings across packs could silently disable
// content in a different pack.
//
// Keys use the stable pack name instead of its per-build identifier, preserving
// user preferences across successive builds of the same pack.
//
// Policy is expressed over a plain value type plus isolated file I/O, allowing
// tests to run without Vulkan, a session, or a window.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <filesystem>
#include <string>

namespace ayther {

struct PlayerConfig {
    /// Selected pack profile; empty means use the pack default.
    std::string profile;
    /// Subsystem mask, applied only when `have_subsystems` is true.
    /// This distinguishes an explicit "disable all" choice from an unset value.
    uint32_t subsystems      = 0;
    bool     have_subsystems = false;
    /// Gain and mute state by `AudioBus` index.
    float bus_gain[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  bus_muted[4] = {false, false, false, false};
    /// Enable presentation post-processing such as CRT and scanline effects.
    bool shaders_on = true;
    /// User-selected output profile (#296), such as `crt`, `lcd`, or `pixel`.
    /// Empty means no explicit choice, allowing the active pack to recommend a
    /// profile without converting that recommendation into a persistent choice.
    std::string output;
    /// Master switch for HD presentation.
    bool hd_on = true;
};

/// Build the configuration path for a game-and-pack pair.
/// @param dir Runtime data directory.
/// @param game_id Stable game identifier.
/// @param pack_name Pack name; empty for a session without a pack.
std::filesystem::path player_config_path(const std::filesystem::path& dir,
                                         const std::string& game_id,
                                         const std::string& pack_name);

/// Load persisted preferences, returning defaults when the file is absent.
///
/// Parsing is best-effort: recognized fields read before malformed content may
/// remain applied. The current parser does not return diagnostics; callers that
/// need strict validation should address the limitation in the code-quality
/// review before relying on the file as a trusted configuration source.
PlayerConfig player_config_load(const std::filesystem::path& file);
/// Persist preferences, creating parent directories as needed.
/// @return `true` only when the complete file was written successfully.
bool         player_config_save(const std::filesystem::path& file,
                                const PlayerConfig& cfg);

/// Sanitize untrusted text for use as a portable file-name component.
///
/// Characters outside `[A-Za-z0-9._-]` are collapsed into `_`. This prevents
/// author-controlled game or pack labels from creating invalid paths.
std::string config_key_sanitize(const std::string& s);

}  // namespace ayther
