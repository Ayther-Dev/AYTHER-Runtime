#include "output_profile.h"

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const char* description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

}  // namespace

int main() {
    using ayther::runtime::OutputScaling;
    using ayther::runtime::output_profile_by_id;
    using ayther::runtime::output_profile_default;
    using ayther::runtime::output_profile_resolve;
    using ayther::runtime::output_profiles;
    using ayther::runtime::output_rect;
    using ayther::runtime::output_shader;

    std::printf("== output_profile_test (MIG-021) ==\n");

    const auto profiles = output_profiles();
    check(profiles.size() == 6, "the Runtime exposes the six product profiles");
    for (const std::string_view id :
         {"lcd", "crt", "pixel", "smooth", "cinema", "ntsc"}) {
        check(output_profile_by_id(id) != nullptr, "a documented id resolves");
    }
    check(output_profile_by_id("hologram") == nullptr,
          "an unknown profile remains distinguishable from the default");
    check(output_profile_by_id({}) == nullptr,
          "an empty profile id is not a selection");

    check(output_profile_resolve("pixel", "crt").id == "pixel",
          "an explicit user choice wins over a pack recommendation");
    check(output_profile_resolve({}, "crt").id == "crt",
          "the pack recommendation applies without a user choice");
    check(output_profile_resolve("unknown", "crt").id == "crt",
          "an unknown user choice falls through to the recommendation");
    check(output_profile_resolve({}, "unknown").id == "lcd",
          "an unknown recommendation falls through to the Runtime default");
    check(output_profile_default().id == "lcd",
          "LCD is the neutral Runtime default");

    const auto* pixel = output_profile_by_id("pixel");
    const auto* lcd = output_profile_by_id("lcd");
    check(pixel != nullptr && pixel->scaling == OutputScaling::Integer,
          "pixel-perfect requests integer scaling");
    if (pixel != nullptr && lcd != nullptr) {
        const auto integer = output_rect(*pixel, 320, 240, 1280, 720);
        check(integer.x == 160 && integer.y == 0
                  && integer.w == 960 && integer.h == 720,
              "integer scaling uses the largest centered whole multiple");

        const auto tiny = output_rect(*pixel, 320, 240, 200, 150);
        check(tiny.x == 0 && tiny.y == 0 && tiny.w == 200 && tiny.h == 150,
              "integer scaling falls back to fit instead of cropping");

        const auto fit = output_rect(*lcd, 320, 240, 1000, 700);
        check(fit.x == 33 && fit.y == 0 && fit.w == 933 && fit.h == 700,
              "fit preserves aspect ratio and centers the frame");
    }

    const auto* crt = output_profile_by_id("crt");
    const auto* cinema = output_profile_by_id("cinema");
    const auto* ntsc = output_profile_by_id("ntsc");
    const auto* smooth = output_profile_by_id("smooth");
    check(crt != nullptr && crt->smoothing,
          "the CRT profile owns its linear-filter decision");
    check(lcd != nullptr && !lcd->smoothing,
          "native LCD presentation remains nearest-filtered");
    check(pixel != nullptr && !pixel->smoothing,
          "pixel-perfect presentation remains nearest-filtered");
    check(smooth != nullptr && smooth->smoothing,
          "the smooth profile selects linear filtering");
    check(cinema != nullptr && cinema->scan_scale == 0.0f
              && cinema->vignette_scale > 0.0f,
          "cinematic presentation uses vignette without scanlines");
    check(ntsc != nullptr && crt != nullptr
              && ntsc->crt_scale == crt->crt_scale
              && ntsc->scan_scale == crt->scan_scale,
          "NTSC inherits the CRT tube policy and adds the cable effect");

    if (crt != nullptr && lcd != nullptr && cinema != nullptr && ntsc != nullptr) {
        const auto authored = output_shader(*crt, 0.4f, 0.5f, 0.25f);
        check(authored.crt == 0.4f && authored.scan == 0.5f
                  && authored.vignette == 0.25f,
              "authored shader values are scaled instead of replaced");

        const auto no_pack = output_shader(*crt, 0.0f, 0.5f, 0.2f);
        check(no_pack.crt > 0.0f && no_pack.scan > 0.0f,
              "the CRT profile remains visible without a pack");
        check(output_shader(*lcd, 0.0f, 0.5f, 0.2f).crt == 0.0f,
              "the LCD profile adds no presentation effect");
        check(output_shader(*crt, 0.4f, 0.0f, 0.0f).scan == 0.0f,
              "partial pack authorship is not filled by profile floors");
        check(output_shader(*cinema, 0.0f, 0.5f, 0.2f).scan == 0.0f,
              "cinematic presentation stays scanline-free without a pack");
        const auto ntsc_no_pack = output_shader(*ntsc, 0.0f, 0.5f, 0.2f);
        check(ntsc_no_pack.crt == no_pack.crt
                  && ntsc_no_pack.scan == no_pack.scan,
              "NTSC inherits CRT floors for games without a pack");
        check(ntsc_no_pack.ntsc > 0.0f,
              "NTSC owns its absolute composite-signal effect");
    }

    std::size_t profiles_with_ntsc = 0;
    for (const auto& profile : profiles) {
        if (profile.ntsc > 0.0f) {
            ++profiles_with_ntsc;
        }
    }
    check(profiles_with_ntsc == 1,
          "only the NTSC profile enables chroma bleed");

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
