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
    append_u32(bytes, qa::current_recording_version);
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

} // namespace

int main() {
    try {
        const auto valid = recording();
        const auto decoded = qa::decode_recording_layout(valid);
        require(decoded.error == qa::RecordingLayoutError::none &&
                    decoded.layout.header.version == 8 && decoded.layout.game_id.offset == 12 &&
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

        std::puts("recording_layout_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_layout_test: %s\n", error.what());
        return 1;
    }
}
