#include "recording_header.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::byte> &bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

std::vector<std::byte> recording(const std::uint32_t frames = 2,
                                 const std::uint32_t raw_state_bytes = 4,
                                 const std::uint32_t compressed_bytes = 3) {
    std::vector<std::byte> bytes{std::byte{0x41}, std::byte{0x52}, std::byte{0x50},
                                 std::byte{0x31}};
    // v9, the shadow version these cases are written for; v10 is built from it below.
    append_u32(bytes, qa::recording_fm_registers_version);
    append_u32(bytes, 3);
    bytes.insert(bytes.end(), {std::byte{'g'}, std::byte{'i'}, std::byte{'d'}});
    append_u32(bytes, 4);
    bytes.insert(bytes.end(), {std::byte{'t'}, std::byte{'a'}, std::byte{'k'}, std::byte{'e'}});
    append_u32(bytes, frames);
    append_u32(bytes, raw_state_bytes);
    append_u32(bytes, compressed_bytes);
    bytes.insert(bytes.end(), compressed_bytes, std::byte{0x5a});
    bytes.insert(bytes.end(), static_cast<std::size_t>(frames) * 2U, std::byte{0});
    return bytes;
}

void set_u32(std::vector<std::byte> &bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void require_error(const qa::RecordingLayoutResult &result, const qa::RecordingLayoutError expected,
                   const std::string_view code, const char *message) {
    require(result.error == expected && qa::recording_layout_error_code(result.error) == code,
            message);
}

// The sections a v9 take carries after its inputs, empty: trim, per-frame stats, sprite and
// audio histories, keyframes, hash algorithm, then the shadow (none, or 512 registers).
std::vector<std::byte> tail(const std::uint32_t frames, const bool with_registers,
                            const std::uint32_t keyframes = 0) {
    std::vector<std::byte> bytes;
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    bytes.insert(bytes.end(), static_cast<std::size_t>(frames) * 12U, std::byte{0});
    append_u32(bytes, 0);
    bytes.insert(bytes.end(), static_cast<std::size_t>(frames) * 4U, std::byte{0});
    append_u32(bytes, keyframes);
    for (std::uint32_t index = 0; index < keyframes; ++index) {
        append_u32(bytes, index);
        append_u32(bytes, 16);
        append_u32(bytes, 2);
        bytes.insert(bytes.end(), {std::byte{0x28}, std::byte{0xb5}});
    }
    append_u32(bytes, 0);
    bytes.insert(bytes.end(), static_cast<std::size_t>(frames) * 4U, std::byte{0});
    append_u32(bytes, 1);
    if (with_registers) {
        append_u32(bytes, qa::recording_fm_register_bytes);
        for (std::uint32_t index = 0; index < qa::recording_fm_register_bytes; ++index)
            bytes.push_back(static_cast<std::byte>(index & 0xffU));
    } else {
        append_u32(bytes, 0);
    }
    return bytes;
}

std::vector<std::byte> with_tail(std::vector<std::byte> bytes, const std::vector<std::byte> &rest) {
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    return bytes;
}

void require_fm_error(const std::vector<std::byte> &take,
                      const qa::RecordingFmRegistersError expected, const std::string_view code,
                      const char *message) {
    const auto decoded = qa::decode_recording_layout(take);
    require(decoded.error == qa::RecordingLayoutError::none, message);
    const auto registers = qa::decode_recording_fm_registers(take, decoded.layout);
    require(registers.error == expected && registers.registers.empty() &&
                qa::recording_fm_registers_error_code(registers.error) == code,
            message);
}

} // namespace

int main() {
    try {
        const auto valid = recording();
        const auto decoded = qa::decode_recording_layout(valid);
        require(decoded.error == qa::RecordingLayoutError::none &&
                    decoded.layout.header.version == 9 && decoded.layout.game_id.offset == 12 &&
                    decoded.layout.game_id.size == 3 && decoded.layout.name.offset == 19 &&
                    decoded.layout.name.size == 4 && decoded.layout.frame_count == 2 &&
                    decoded.layout.raw_state_bytes == 4 &&
                    decoded.layout.compressed_state.offset == 35 &&
                    decoded.layout.compressed_state.size == 3 &&
                    decoded.layout.inputs.offset == 38 && decoded.layout.inputs.size == 4,
                "valid_layout_changed");

        require_error(qa::decode_recording_layout({}, qa::max_recording_bytes + 1U),
                      qa::RecordingLayoutError::take_too_large, "take_too_large",
                      "oversized_take_not_rejected_before_read");

        for (std::size_t size = qa::recording_fixed_header_bytes; size < valid.size(); ++size) {
            require_error(
                qa::decode_recording_layout(std::span<const std::byte>{valid}.first(size)),
                qa::RecordingLayoutError::truncated_content, "truncated_content",
                "truncated_recording_not_rejected");
        }

        auto changed = recording(0);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::invalid_frame_count, "invalid_frame_count",
                      "empty_input_stream_accepted");
        changed = recording(qa::max_recording_frames + 1U);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::invalid_frame_count, "invalid_frame_count",
                      "excessive_frame_count_accepted");

        changed = recording(2, 0);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::empty_initial_state, "empty_initial_state",
                      "empty_state_accepted");
        changed = recording(2, qa::max_recording_state_bytes + 1U);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::initial_state_too_large, "initial_state_too_large",
                      "excessive_state_accepted");

        changed = recording();
        set_u32(changed, 31, 0);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::empty_compressed_state, "empty_compressed_state",
                      "empty_compressed_state_accepted");

        changed = recording();
        set_u32(changed, 8, 0xffffffffU);
        require_error(qa::decode_recording_layout(changed),
                      qa::RecordingLayoutError::truncated_content, "truncated_content",
                      "oversized_game_id_accepted");

        const auto bad_header =
            qa::decode_recording_layout(std::span<const std::byte>{valid}.first(4));
        require(bad_header.error == qa::RecordingLayoutError::invalid_header &&
                    bad_header.header_error == qa::RecordingHeaderError::truncated_header,
                "header_diagnostic_lost");

        // v9 shadow: present, absent, and malformed tails read like the Engine loader.
        {
            const auto shadowed = with_tail(recording(), tail(2, true));
            const auto layout = qa::decode_recording_layout(shadowed);
            const auto registers = qa::decode_recording_fm_registers(shadowed, layout.layout);
            require(registers.error == qa::RecordingFmRegistersError::none &&
                        registers.registers.size() == qa::recording_fm_register_bytes &&
                        registers.registers[1] == 1 && registers.registers[511] == 0xff,
                    "fm_registers_not_decoded");

            const auto keyframed = with_tail(recording(), tail(2, true, 2));
            const auto keyframed_registers = qa::decode_recording_fm_registers(
                keyframed, qa::decode_recording_layout(keyframed).layout);
            require(keyframed_registers.error == qa::RecordingFmRegistersError::none &&
                        keyframed_registers.registers.size() == qa::recording_fm_register_bytes,
                    "fm_registers_lost_behind_keyframes");

            const auto bare = with_tail(recording(), tail(2, false));
            const auto none =
                qa::decode_recording_fm_registers(bare, qa::decode_recording_layout(bare).layout);
            require(none.error == qa::RecordingFmRegistersError::none && none.registers.empty(),
                    "absent_shadow_not_empty");

            auto older = with_tail(recording(), tail(2, false));
            older.resize(older.size() - 4U);
            set_u32(older, 4, 8);
            const auto older_layout = qa::decode_recording_layout(older);
            require(older_layout.error == qa::RecordingLayoutError::none &&
                        older_layout.layout.header.version == 8,
                    "v8_take_rejected");
            const auto v8 = qa::decode_recording_fm_registers(older, older_layout.layout);
            require(v8.error == qa::RecordingFmRegistersError::none && v8.registers.empty(),
                    "v8_take_has_registers");

            auto truncated = with_tail(recording(), tail(2, true));
            truncated.resize(truncated.size() - 1U);
            require_fm_error(truncated, qa::RecordingFmRegistersError::truncated_content,
                             "truncated_content", "truncated_shadow_accepted");

            require_fm_error(recording(), qa::RecordingFmRegistersError::truncated_content,
                             "truncated_content", "missing_sections_accepted");

            auto odd_size = with_tail(recording(), tail(2, false));
            set_u32(odd_size, odd_size.size() - 4U, 16);
            odd_size.insert(odd_size.end(), 16, std::byte{0});
            require_fm_error(odd_size, qa::RecordingFmRegistersError::invalid_size, "invalid_size",
                             "wrong_shadow_size_accepted");

            auto trailing = with_tail(recording(), tail(2, true));
            trailing.push_back(std::byte{0});
            require_fm_error(trailing, qa::RecordingFmRegistersError::trailing_content,
                             "trailing_content", "trailing_bytes_accepted");

            // v10 (Engine spec 007): the take id (u64) follows the shadow.
            auto v10 = with_tail(recording(), tail(2, true));
            set_u32(v10, 4, qa::take_id_recording_version);
            v10.insert(v10.end(), qa::take_id_bytes, std::byte{0x7c});
            const auto v10_registers =
                qa::decode_recording_fm_registers(v10, qa::decode_recording_layout(v10).layout);
            require(v10_registers.error == qa::RecordingFmRegistersError::none &&
                        v10_registers.registers.size() == qa::recording_fm_register_bytes &&
                        v10_registers.registers[511] == 0xff,
                    "v10_shadow_not_decoded");
            auto v10_short = with_tail(recording(), tail(2, true));
            set_u32(v10_short, 4, qa::take_id_recording_version);
            v10_short.insert(v10_short.end(), qa::take_id_bytes - 1U, std::byte{0});
            require_fm_error(v10_short, qa::RecordingFmRegistersError::truncated_content,
                             "truncated_content", "truncated_take_id_accepted");

            // Keyframe count sits after inputs (42), trim (8), stats (2 x 12), sprite history
            // total (4) and counts (2 x 4).
            auto bad_keyframes = with_tail(recording(), tail(2, true));
            set_u32(bad_keyframes, 42U + 8U + 24U + 4U + 8U, 4);
            require_fm_error(bad_keyframes, qa::RecordingFmRegistersError::invalid_section,
                             "invalid_section", "excess_keyframes_accepted");
        }

        std::puts("recording_layout_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_layout_test: %s\n", error.what());
        return 1;
    }
}
