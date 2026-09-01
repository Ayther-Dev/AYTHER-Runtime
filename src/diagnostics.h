#pragma once
// ---------------------------------------------------------------------------
// diagnostics.h — audiovisual diagnostics assistant (#302).
//
// A short local test measures available capabilities and SUGGESTS an output
// profile (#296). It never applies the recommendation: the user remains the
// decision maker.
//
// PRIVACY BOUNDARY: this component has no network path. Reports are written to
// a local file and are never transmitted. Any future upload capability would
// require a new, explicit implementation and consent flow.
//
// Measurement belongs to the runtime because it requires SDL and Vulkan.
// Recommendation policy is pure and isolated here so it can be tested without
// graphics hardware.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>

namespace ayther {

/// Measurements and the recommendation derived from them.
///
/// Numeric zero represents unavailable data unless a field says otherwise.
/// The recommendation logic treats missing data conservatively.
struct DiagnosticReport {
    // Display
    uint32_t display_w = 0, display_h = 0;
    float    refresh_hz = 0.0f;
    /// Largest integer scale at which the native 320x240 frame fits.
    /// Zero means the value could not be measured.
    uint32_t integer_scale = 0;

    // Performance
    /// Measured milliseconds per frame; zero means unavailable.
    float    frame_ms = 0.0f;

    // Audio
    int      audio_freq     = 0;
    int      audio_channels = 0;
    /// Output-buffer duration in milliseconds.
    ///
    /// This is buffer size divided by sample rate, not end-to-end audio
    /// latency. Drivers, the operating system, and playback hardware add delay
    /// that cannot be measured accurately without external reference hardware.
    float    buffer_latency_ms = 0.0f;

    // Graphics
    bool        vulkan = false;
    std::string gpu_name;

    /// Suggested output-profile identifier (#296) and its human-readable basis.
    std::string suggested;
    std::string reason;
};

/// Derive an output-profile suggestion from the report's measurements.
///
/// Rules are evaluated in policy order and kept in one place so UI surfaces
/// cannot diverge. This function performs no I/O.
/// @post `r.suggested` and `r.reason` describe the selected recommendation.
void diagnose_suggest(DiagnosticReport& r);

/// Format a local Markdown report without performing I/O or network access.
std::string diagnostic_report_markdown(const DiagnosticReport& r);

}  // namespace ayther
