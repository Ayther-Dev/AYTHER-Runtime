// Spec 002 (contracts.md C5, «Sondeo de pack del Runtime»; RF-2.2): the pure part of the
// pack probe. Catalogs are read as the Engine reads them, assets that announce an image
// or a WAV file must decode, and the probe line says why a pack cannot be used.
#include "pack_probe.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

void check(const bool condition, const char *description) {
    if (condition)
        ++passed;
    else
        ++failed;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

// The 1x1 pose image of tests/audio_qa/fixtures/public-synthetic-poses.ay.
constexpr std::array<std::uint8_t, 70> pose_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
    0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
    0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7,
    0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

// The same image whose IDAT is not a zlib stream (public-synthetic-corrupt-asset.ay).
constexpr std::array<std::uint8_t, 74> corrupt_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
    0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x11, 0x49, 0x44, 0x41, 0x54, 0x6e, 0x6f, 0x74, 0x20,
    0x61, 0x20, 0x7a, 0x6c, 0x69, 0x62, 0x20, 0x73, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x45, 0x00,
    0xdc, 0x51, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

// The 16-frame stereo PCM tone of the public audio fixture.
constexpr std::array<std::uint8_t, 60> tone_wav{
    82, 73, 70, 70, 52, 0,   0, 0, 87, 65,  86, 69, 102, 109, 116, 32, 16,  0,  0,   0,
    1,  0,  2,  0,  68, 172, 0, 0, 16, 177, 2,  0,  4,   0,   16,  0,  100, 97, 116, 97,
    16, 0,  0,  0,  1,  0,   1, 0, 2,  0,   2,  0,  3,   0,   3,   0,  4,   0,  4,   0};

void catalogs_are_read_as_the_engine_reads_them() {
    using ayther::runtime::read_pack_catalog;
    const auto catalog = read_pack_catalog(
        R"([[pose]]
hashes = ["0x1"]
asset = "pose-a"
mask = "mask-a"
[[pose.variant]]
palette = 1
asset = "variant-a"
[[pose]]
hashes = ["0x2"]
asset = "pose-a"
)",
        R"([[event]]
signature = "0xdaf281b060d3fe48"
asset = "tone"
[[event]]
signature = "1f"
asset = ""
)");
    check(catalog && catalog->poses == 2U && catalog->audio_events == 2U &&
              catalog->assets == std::vector<std::string>{"pose-a", "mask-a", "variant-a", "tone"},
          "poses, masks, variants and events are counted and their assets listed once");
    check(read_pack_catalog({}, {}) == ayther::runtime::PackCatalog{},
          "a pack without catalogs has empty catalogs");
    check(!read_pack_catalog("[[pose]]\nasset = \"a\"\n", {}) &&
              !read_pack_catalog("[[pose]]\nhashes = [\"0x1\"]\n", {}) &&
              !read_pack_catalog("pose = 3\n", {}) &&
              !read_pack_catalog("[[pose]]\nhashes = [\"0x1\"]\nasset = \"a\"\n"
                                 "[[pose.variant]]\npalette = 1\n",
                                 {}),
          "a pose catalog the Engine would drop is invalid");
    check(!read_pack_catalog({}, "[[event]]\nsignature = \"zz\"\nasset = \"a\"\n") &&
              !read_pack_catalog({}, "[[event]]\nsignature = \"0x1\"\n") &&
              !read_pack_catalog({}, "title = \"no events\"\n") &&
              !read_pack_catalog({}, "[[event]\n"),
          "an audio event the Engine would skip is reported, not lost");
}

void assets_must_decode() {
    using ayther::runtime::asset_decodes;
    check(asset_decodes(pose_png), "a valid PNG decodes");
    check(!asset_decodes(corrupt_png), "a PNG whose image data is corrupt does not decode");
    check(asset_decodes(tone_wav), "a valid WAV file decodes");
    auto truncated = std::vector<std::uint8_t>(tone_wav.begin(), tone_wav.end() - 4);
    check(!asset_decodes(truncated), "a WAV file shorter than its chunks does not decode");
    constexpr std::array<std::uint8_t, 4> lua{'-', '-', ' ', 'x'};
    check(asset_decodes(lua), "other formats only need to be readable");
}

void probe_line_states_the_reason() {
    using ayther::runtime::PackProbeReport;
    PackProbeReport usable;
    usable.opened = true;
    usable.signed_pack = true;
    usable.signature = "valid";
    usable.trust = "trusted";
    usable.catalog = ayther::runtime::PackCatalog{217U, 14U, {}};
    usable.game_id = "crc32:665d7df9";
    check(ayther::runtime::pack_probe_reason(usable).empty() &&
              ayther::runtime::format_pack_probe_line(usable) ==
                  "AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":true,\"signature\":\"valid\","
                  "\"trust\":\"trusted\",\"catalog\":{\"poses\":217,\"audio_events\":14},"
                  "\"unreadable_assets\":[],\"game_id\":\"crc32:665d7df9\",\"errors\":[]}\n",
          "a usable pack has the contract line and no reason");

    auto unverified = usable;
    unverified.signature = "unverified";
    unverified.trust = "unverified";
    check(ayther::runtime::pack_probe_reason(unverified) == "pack_trust_unverified",
          "a signed pack without a trust registry cannot be used");
    auto unsigned_pack = unverified;
    unsigned_pack.signed_pack = false;
    unsigned_pack.signature = "unsigned";
    check(ayther::runtime::pack_probe_reason(unsigned_pack).empty(),
          "an unsigned pack the Engine policy opened can be used");

    auto corrupt = usable;
    corrupt.unreadable_assets = {"bf4a1789ea6a189f925e9d211f8289f2"};
    const auto line = ayther::runtime::format_pack_probe_line(corrupt);
    check(ayther::runtime::pack_probe_reason(corrupt) == "pack_assets_unreadable" &&
              line.find("\"unreadable_assets\":[\"bf4a1789ea6a189f925e9d211f8289f2\"]") !=
                  std::string::npos &&
              line.find("\"reason\":\"pack_assets_unreadable\"") != std::string::npos,
          "an undecodable asset is listed and makes the pack unusable");

    PackProbeReport closed;
    closed.errors = {"zip"};
    closed.message = "no es un .ay \"legible\"";
    const auto closed_line = ayther::runtime::format_pack_probe_line(closed);
    check(ayther::runtime::pack_probe_reason(closed) == "pack_invalid" &&
              closed_line.find("\"catalog\":null") != std::string::npos &&
              closed_line.find("\"errors\":[\"zip\"]") != std::string::npos &&
              closed_line.find("\"message\":\"no es un .ay \\\"legible\\\"\"") != std::string::npos,
          "an invalid pack keeps the Engine codes and message, escaped");
    auto unreadable_catalog = usable;
    unreadable_catalog.catalog.reset();
    check(ayther::runtime::pack_probe_reason(unreadable_catalog) == "pack_catalog_invalid",
          "a catalog that cannot be read makes the pack unusable");
}

} // namespace

int main() {
    std::printf("== pack_probe_test (spec 002, BR-056) ==\n");
    catalogs_are_read_as_the_engine_reads_them();
    assets_must_decode();
    probe_line_states_the_reason();
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
