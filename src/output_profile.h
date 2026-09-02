#pragma once
// ---------------------------------------------------------------------------
// output_profile.h — Runtime-owned window presentation policy.
//
// Output profiles describe how a composed frame reaches the player's window:
// scaling, filtering, and presentation effects. They do not decide which pack
// assets Engine substitutes and therefore are deliberately local to Runtime.
//
// The policy is header-only, immutable, and Vulkan-free so its full behavior
// can be tested without a window, GPU, or Engine target.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ayther::runtime {

enum class OutputScaling : std::uint8_t {
    Fit,
    Integer,
};

struct OutputProfile {
    std::string_view id;
    std::string_view name;
    OutputScaling scaling{OutputScaling::Fit};
    bool smoothing{false};

    // Multipliers applied when a pack authors the CRT presentation effect.
    float crt_scale{0.0f};
    float scan_scale{0.0f};
    float vignette_scale{0.0f};

    // Runtime presentation values used when the pack does not enable CRT.
    float crt_base{0.0f};
    float scan_base{0.0f};
    float vignette_base{0.0f};

    // Absolute composite-signal chroma bleed. Packs do not author this value.
    float ntsc{0.0f};
};

namespace detail {

inline constexpr std::array<OutputProfile, 6> output_profile_table{{
    // LCD is neutral on a modern display and remains the default.
    {"lcd", "LCD nativo", OutputScaling::Fit, false,
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // Simulated CRT: authored effects pass through; restrained floors keep the
    // profile meaningful for games without a pack.
    {"crt", "CRT simulado", OutputScaling::Fit, true,
     1.0f, 1.0f, 1.0f, 1.0f, 0.35f, 0.15f, 0.0f},
    {"pixel", "Pixel-perfect", OutputScaling::Integer, false,
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {"smooth", "Suavizado", OutputScaling::Fit, true,
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // Cinematic is presentation rather than television emulation: vignette and
    // slight curvature, without scanlines.
    {"cinema", "Cinematográfica", OutputScaling::Fit, true,
     0.35f, 0.0f, 1.0f, 0.5f, 0.0f, 0.5f, 0.0f},
    // NTSC combines the CRT tube with composite-signal chroma bleed.
    {"ntsc", "NTSC compuesto", OutputScaling::Fit, true,
     1.0f, 1.0f, 1.0f, 1.0f, 0.35f, 0.15f, 1.0f},
}};

}  // namespace detail

[[nodiscard]] constexpr std::span<const OutputProfile> output_profiles() noexcept {
    return detail::output_profile_table;
}

[[nodiscard]] constexpr const OutputProfile* output_profile_by_id(
    std::string_view id) noexcept {
    if (id.empty()) {
        return nullptr;
    }
    for (const auto& profile : output_profiles()) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr const OutputProfile& output_profile_default() noexcept {
    return detail::output_profile_table.front();
}

// Explicit/persisted user choice > pack recommendation > Runtime default.
// Unknown future ids fall through instead of invalidating a pack or session.
[[nodiscard]] constexpr const OutputProfile& output_profile_resolve(
    std::string_view user_choice,
    std::string_view pack_recommendation) noexcept {
    if (const auto* profile = output_profile_by_id(user_choice)) {
        return *profile;
    }
    if (const auto* profile = output_profile_by_id(pack_recommendation)) {
        return *profile;
    }
    return output_profile_default();
}

struct OutputShader {
    float crt;
    float scan;
    float vignette;
    float ntsc;
};

// pack_crt is the authorship gate. Script defaults may make pack_scan and
// pack_vignette non-zero even when a pack did not enable CRT.
[[nodiscard]] constexpr OutputShader output_shader(
    const OutputProfile& profile,
    float pack_crt,
    float pack_scan,
    float pack_vignette) noexcept {
    if (pack_crt > 0.0f) {
        return {
            pack_crt * profile.crt_scale,
            pack_scan * profile.scan_scale,
            pack_vignette * profile.vignette_scale,
            profile.ntsc,
        };
    }
    return {
        profile.crt_base,
        profile.scan_base,
        profile.vignette_base,
        profile.ntsc,
    };
}

struct OutputRect {
    std::int32_t x;
    std::int32_t y;
    std::int32_t w;
    std::int32_t h;
};

// Integer scaling uses the largest whole native-frame multiple. If 1x does not
// fit, the function falls through to aspect-preserving Fit instead of cropping.
[[nodiscard]] constexpr OutputRect output_rect(
    const OutputProfile& profile,
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::uint32_t destination_width,
    std::uint32_t destination_height) noexcept {
    if (source_width == 0 || source_height == 0
        || destination_width == 0 || destination_height == 0) {
        return {
            0,
            0,
            static_cast<std::int32_t>(destination_width),
            static_cast<std::int32_t>(destination_height),
        };
    }

    if (profile.scaling == OutputScaling::Integer) {
        const std::uint32_t horizontal_scale = destination_width / source_width;
        const std::uint32_t vertical_scale = destination_height / source_height;
        const std::uint32_t scale = horizontal_scale < vertical_scale
            ? horizontal_scale
            : vertical_scale;
        if (scale >= 1) {
            const auto width = static_cast<std::int32_t>(source_width * scale);
            const auto height = static_cast<std::int32_t>(source_height * scale);
            return {
                (static_cast<std::int32_t>(destination_width) - width) / 2,
                (static_cast<std::int32_t>(destination_height) - height) / 2,
                width,
                height,
            };
        }
    }

    const double source_aspect =
        static_cast<double>(source_width) / source_height;
    const double destination_aspect =
        static_cast<double>(destination_width) / destination_height;

    std::int32_t width = 0;
    std::int32_t height = 0;
    if (destination_aspect > source_aspect) {
        height = static_cast<std::int32_t>(destination_height);
        width = static_cast<std::int32_t>(destination_height * source_aspect + 0.5);
    } else {
        width = static_cast<std::int32_t>(destination_width);
        height = static_cast<std::int32_t>(destination_width / source_aspect + 0.5);
    }
    return {
        (static_cast<std::int32_t>(destination_width) - width) / 2,
        (static_cast<std::int32_t>(destination_height) - height) / 2,
        width,
        height,
    };
}

static_assert(output_profiles().size() == 6);
static_assert(output_profile_default().id == "lcd");

}  // namespace ayther::runtime
