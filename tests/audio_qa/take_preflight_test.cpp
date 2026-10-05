// Spec 002: RF-2.2 and RNF-3 (plan §5.2, §8 P-10). The take is validated from the same
// bytes that were pinned: ARP1 versions 2 to 8, 1 to 54 000 frames, the size limit and
// the game it was recorded for. Synthetic takes sit at each limit and just above it.
#include "content_hash.h"
#include "material_preflight.h"
#include "recording_header.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void append_u32(std::vector<std::byte> &bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void append_text(std::vector<std::byte> &bytes, std::string_view text) {
    append_u32(bytes, static_cast<std::uint32_t>(text.size()));
    for (const char character : text)
        bytes.push_back(static_cast<std::byte>(character));
}

// ARP1 layout: magic, version, game id, name, frames, raw and compressed state sizes,
// the compressed state and one 16-bit input per frame.
std::vector<std::byte> make_take(std::uint32_t version, std::uint32_t frames,
                                 std::string_view game_id = "crc32:cbf43926") {
    std::vector<std::byte> bytes{std::byte{'A'}, std::byte{'R'}, std::byte{'P'}, std::byte{'1'}};
    append_u32(bytes, version);
    append_text(bytes, game_id);
    append_text(bytes, "synthetic take");
    append_u32(bytes, frames);
    append_u32(bytes, 16U);
    append_u32(bytes, 4U);
    bytes.insert(bytes.end(), 4U, std::byte{0x5a});
    bytes.insert(bytes.end(), static_cast<std::size_t>(frames) * 2U, std::byte{0});
    return bytes;
}

std::string code(const qa::TakeInspection &inspection) {
    const auto *error = std::get_if<std::string>(&inspection);
    return error != nullptr ? *error : std::string{};
}

void frame_count_limits() {
    const auto one = qa::inspect_take(make_take(8, 1));
    const auto *facts = std::get_if<qa::TakeFacts>(&one);
    expect(facts != nullptr && *facts == qa::TakeFacts{8, 1, "crc32:cbf43926", "synthetic take"},
           "RF-2.2: a one-frame take is valid and declares its game");
    const auto at_limit = qa::inspect_take(make_take(8, qa::max_recording_frames));
    expect(std::holds_alternative<qa::TakeFacts>(at_limit) &&
               std::get<qa::TakeFacts>(at_limit).frame_count == 54'000U,
           "RNF-3: 54 000 frames are accepted");
    expect(code(qa::inspect_take(make_take(8, qa::max_recording_frames + 1U))) ==
               "take_invalid_frame_count",
           "RNF-3: 54 001 frames are rejected");
    expect(code(qa::inspect_take(make_take(8, 0))) == "take_invalid_frame_count",
           "RNF-3: a take without frames is rejected");
}

void header_versions() {
    expect(std::holds_alternative<qa::TakeFacts>(qa::inspect_take(make_take(2, 3))) &&
               std::holds_alternative<qa::TakeFacts>(qa::inspect_take(make_take(8, 3))),
           "RF-2.2: ARP1 versions 2 and 8 are accepted");
    expect(code(qa::inspect_take(make_take(1, 3))) == "take_unsupported_version" &&
               code(qa::inspect_take(make_take(9, 3))) == "take_unsupported_version",
           "RF-2.2: versions 1 and 9 are rejected");
    auto magic = make_take(8, 3);
    magic[3] = std::byte{'X'};
    expect(code(qa::inspect_take(magic)) == "take_invalid_magic",
           "RF-2.2: another magic is rejected");
    expect(code(qa::inspect_take({})) == "take_empty", "RF-2.2: an empty take is rejected");
    const auto take = make_take(8, 3);
    expect(code(qa::inspect_take(std::span{take}.first(6))) == "take_truncated_header",
           "RF-2.2: a truncated header is rejected");
    expect(code(qa::inspect_take(std::span{take}.first(take.size() - 1U))) == "take_truncated",
           "RF-2.2: a take missing its last input is rejected");
}

void size_limit_applies_before_reading() {
    const auto path = std::filesystem::temp_directory_path() / "ayther-qa-take-preflight.arp";
    const auto take = make_take(8, 5);
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output.write(reinterpret_cast<const char *>(take.data()),
                     static_cast<std::streamsize>(take.size()));
    }
    const auto at_limit =
        qa::pin_material_content(qa::MaterialPinRole::take, "--take", path, 1, take.size());
    const auto *pinned = std::get_if<qa::PinnedContent>(&at_limit);
    expect(pinned != nullptr && pinned->bytes == take &&
               pinned->pin.content == qa::identify_content(take) && pinned->pin.position == 1,
           "RNF-3: a take at the size limit is pinned with the bytes it was hashed from");
    const auto above =
        qa::pin_material_content(qa::MaterialPinRole::take, "--take", path, 1, take.size() - 1U);
    const auto *issue = std::get_if<qa::MaterialIssue>(&above);
    expect(issue != nullptr &&
               *issue == qa::MaterialIssue{"--take", qa::MaterialPinError::too_large},
           "RNF-3: a take above the size limit is rejected without reading it");
    expect(issue != nullptr &&
               qa::field_issue(*issue) == qa::FieldIssue{"--take", "material_too_large"},
           "RF-2.2: the rejection names its field and code");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void crc32_names_the_rom() {
    const auto path = std::filesystem::temp_directory_path() / "ayther-qa-take-preflight.rom";
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "123456789";
    }
    const auto result = qa::pin_material(qa::MaterialPinRole::rom, "--rom", path);
    const auto *pin = std::get_if<qa::MaterialPin>(&result);
    expect(pin != nullptr && pin->crc32 == 0xcbf43926U &&
               qa::crc32_game_id(pin->crc32) == "crc32:cbf43926",
           "RF-2.2: the ROM CRC-32 is the IEEE checksum used by packs and takes");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// contracts.md C5, «Sondeo del core con la ROM»: the duration uses the timing the core
// reports for the ROM. 54 000 frames fit at 60 fps but not at 59.92 or at 49.70.
void duration_limit() {
    constexpr double ntsc = 59.922743;
    constexpr double pal = 49.701459;
    expect(!qa::take_duration_issue(54'000, 60.0), "RNF-3: 54 000 frames at 60 fps are 900 s");
    expect(!qa::take_duration_issue(53'930, ntsc) &&
               qa::take_duration_issue(53'931, ntsc) == qa::FieldIssue{"--take", "take_too_long"},
           "RNF-3: at 59.92 fps the limit is 53 930 frames");
    expect(qa::take_duration_issue(54'000, ntsc) == qa::FieldIssue{"--take", "take_too_long"},
           "RNF-3: 54 000 frames at 59.92 fps exceed 900 s");
    expect(!qa::take_duration_issue(44'731, pal) &&
               qa::take_duration_issue(44'732, pal) == qa::FieldIssue{"--take", "take_too_long"},
           "RNF-3: at 49.70 fps the limit is 44 731 frames");
    expect(qa::take_duration_issue(100, std::nullopt) ==
                   qa::FieldIssue{"--core", "core_timing_unknown"} &&
               qa::take_duration_issue(100, 0.0) == qa::FieldIssue{"--core", "core_timing_unknown"},
           "RF-2.2: without a valid timing the duration cannot be validated");
}

void game_identity() {
    constexpr std::uint32_t rom = 0x665d7df9U;
    expect(!qa::take_game_issue("crc32:665d7df9", rom, std::nullopt) &&
               !qa::take_game_issue("CRC32:665D7DF9", rom, std::nullopt),
           "RF-2.2: a take of the selected ROM is accepted");
    expect(qa::take_game_issue("crc32:00000001", rom, std::nullopt) == "take_game_mismatch",
           "RF-1.7: a take of another ROM is an incompatibility");
    expect(!qa::take_game_issue("", rom, std::string_view{"crc32:665d7df9"}),
           "RF-2.2: a take that declares no game is not compared");
    expect(!qa::take_game_issue("public-synthetic", rom, std::string_view{"public-synthetic"}) &&
               qa::take_game_issue("public-synthetic", rom, std::string_view{"other"}) ==
                   "take_game_mismatch",
           "RF-1.7: a take named by its pack must match the selected pack");
    expect(!qa::take_game_issue("public-synthetic", rom, std::nullopt),
           "RF-1.3: a take recorded with a pack can be replayed without one");
    expect(!qa::pack_game_issue("crc32:665d7df9", rom) &&
               qa::pack_game_issue("crc32:00000001", rom) == "pack_game_mismatch" &&
               !qa::pack_game_issue("public-synthetic", rom),
           "RF-1.7: a pack of another ROM is an incompatibility");
}

} // namespace

// A take whose 16-byte initial state is one zstd frame with one raw block: magic, a frame
// header (single segment, one-byte content size), the block header and the bytes.
std::vector<std::byte> take_with_state(std::uint8_t declared_size = 16U) {
    std::vector<std::byte> frame{std::byte{0x28}, std::byte{0xb5}, std::byte{0x2f},
                                 std::byte{0xfd}, std::byte{0x20}, std::byte{declared_size}};
    constexpr std::uint32_t block = (16U << 3U) | 1U; // last block, raw, 16 bytes
    frame.push_back(static_cast<std::byte>(block & 0xffU));
    frame.push_back(static_cast<std::byte>((block >> 8U) & 0xffU));
    frame.push_back(static_cast<std::byte>((block >> 16U) & 0xffU));
    frame.insert(frame.end(), 16U, std::byte{0x5a});

    std::vector<std::byte> bytes{std::byte{'A'}, std::byte{'R'}, std::byte{'P'}, std::byte{'1'}};
    append_u32(bytes, 8U);
    append_text(bytes, "crc32:cbf43926");
    append_text(bytes, "synthetic take");
    append_u32(bytes, 3U);
    append_u32(bytes, 16U);
    append_u32(bytes, static_cast<std::uint32_t>(frame.size()));
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    bytes.insert(bytes.end(), 6U, std::byte{0});
    return bytes;
}

// D-2 (campaign 2026-10-04, BR-181; RF-2.2): the initial state is decompressed whole before
// admission, as the Runtime will before restoring it.
void initial_state_is_checked() {
    const auto valid = take_with_state();
    expect(std::holds_alternative<qa::TakeFacts>(qa::inspect_take(valid)) &&
               !qa::take_state_issue(valid),
           "D-2: a take whose state decompresses to its declared size is valid");
    auto damaged = valid;
    const auto state_offset = valid.size() - 6U - 25U;
    for (std::size_t index = state_offset; index < state_offset + 9U; ++index)
        damaged[index] = ~damaged[index];
    expect(std::holds_alternative<qa::TakeFacts>(qa::inspect_take(damaged)) &&
               qa::take_state_issue(damaged) == std::string{"take_initial_state_invalid"},
           "D-2: inverted bytes in the compressed state are found before admission");
    expect(qa::take_state_issue(take_with_state(15U)) == std::string{"take_initial_state_invalid"},
           "D-2: a frame that declares another size than the take is damaged");
}

int main() {
    frame_count_limits();
    header_versions();
    size_limit_applies_before_reading();
    crc32_names_the_rom();
    duration_limit();
    game_identity();
    initial_state_is_checked();
    if (failures != 0)
        return 1;
    std::cout << "takes are validated before admission\n";
    return 0;
}
