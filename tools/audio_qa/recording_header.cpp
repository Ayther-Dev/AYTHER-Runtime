#include "recording_header.h"

#include <array>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] std::uint32_t read_u32_le(const std::span<const std::byte> bytes,
                                        const std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] bool has_bytes(const std::uint64_t offset, const std::uint64_t count,
                             const std::uint64_t limit) noexcept {
    return offset <= limit && count <= limit - offset;
}

} // namespace

RecordingHeaderResult decode_recording_header(const std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return {RecordingHeaderError::empty_take, {}};
    }
    if (bytes.size() < recording_fixed_header_bytes) {
        return {RecordingHeaderError::truncated_header, {}};
    }

    constexpr std::array magic{std::byte{0x41}, std::byte{0x52}, std::byte{0x50}, std::byte{0x31}};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != magic[index]) {
            return {RecordingHeaderError::invalid_magic, {}};
        }
    }

    const auto version = read_u32_le(bytes, magic.size());
    if (version < oldest_supported_recording_version || version > current_recording_version) {
        return {RecordingHeaderError::unsupported_version, {version}};
    }
    return {RecordingHeaderError::none, {version}};
}

std::string_view recording_header_error_code(const RecordingHeaderError error) noexcept {
    switch (error) {
    case RecordingHeaderError::none:
        return "none";
    case RecordingHeaderError::empty_take:
        return "empty_take";
    case RecordingHeaderError::truncated_header:
        return "truncated_header";
    case RecordingHeaderError::invalid_magic:
        return "invalid_magic";
    case RecordingHeaderError::unsupported_version:
        return "unsupported_version";
    }
    return "unknown_recording_header_error";
}

RecordingLayoutResult decode_recording_layout(const std::span<const std::byte> bytes) noexcept {
    return decode_recording_layout(bytes, bytes.size());
}

RecordingLayoutResult decode_recording_layout(const std::span<const std::byte> header_bytes,
                                              const std::uint64_t total_bytes) noexcept {
    if (total_bytes > max_recording_bytes) {
        return {RecordingLayoutError::take_too_large, RecordingHeaderError::none, {}};
    }

    const auto decoded_header = decode_recording_header(header_bytes);
    if (decoded_header.error != RecordingHeaderError::none) {
        return {RecordingLayoutError::invalid_header, decoded_header.error, {}};
    }

    const auto read_field = [&](const std::uint64_t offset, std::uint32_t &value) noexcept {
        if (!has_bytes(offset, sizeof(value), total_bytes) ||
            !has_bytes(offset, sizeof(value), header_bytes.size())) {
            return false;
        }
        value = read_u32_le(header_bytes, static_cast<std::size_t>(offset));
        return true;
    };

    RecordingLayout layout{};
    layout.header = decoded_header.header;
    std::uint64_t cursor = recording_fixed_header_bytes;
    std::uint32_t length{};
    if (!read_field(cursor, length)) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    cursor += sizeof(length);
    if (!has_bytes(cursor, length, total_bytes) ||
        !has_bytes(cursor, length, header_bytes.size())) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    layout.game_id = {cursor, length};
    cursor += length;

    if (!read_field(cursor, length)) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    cursor += sizeof(length);
    if (!has_bytes(cursor, length, total_bytes) ||
        !has_bytes(cursor, length, header_bytes.size())) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    layout.name = {cursor, length};
    cursor += length;

    std::uint32_t compressed_state_bytes{};
    if (!read_field(cursor, layout.frame_count) ||
        !read_field(cursor + sizeof(std::uint32_t), layout.raw_state_bytes) ||
        !read_field(cursor + 2U * sizeof(std::uint32_t), compressed_state_bytes)) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    cursor += 3U * sizeof(std::uint32_t);

    if (layout.frame_count < min_recording_frames || layout.frame_count > max_recording_frames) {
        return {RecordingLayoutError::invalid_frame_count, RecordingHeaderError::none, {}};
    }
    if (layout.raw_state_bytes == 0U) {
        return {RecordingLayoutError::empty_initial_state, RecordingHeaderError::none, {}};
    }
    if (layout.raw_state_bytes > max_recording_state_bytes) {
        return {RecordingLayoutError::initial_state_too_large, RecordingHeaderError::none, {}};
    }
    if (compressed_state_bytes == 0U) {
        return {RecordingLayoutError::empty_compressed_state, RecordingHeaderError::none, {}};
    }

    layout.compressed_state = {cursor, compressed_state_bytes};
    if (!has_bytes(cursor, compressed_state_bytes, total_bytes)) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    cursor += compressed_state_bytes;

    const auto input_bytes = static_cast<std::uint64_t>(layout.frame_count) * sizeof(std::uint16_t);
    layout.inputs = {cursor, input_bytes};
    if (!has_bytes(cursor, input_bytes, total_bytes)) {
        return {RecordingLayoutError::truncated_content, RecordingHeaderError::none, {}};
    }
    return {RecordingLayoutError::none, RecordingHeaderError::none, layout};
}

std::string_view recording_layout_error_code(const RecordingLayoutError error) noexcept {
    switch (error) {
    case RecordingLayoutError::none:
        return "none";
    case RecordingLayoutError::invalid_header:
        return "invalid_header";
    case RecordingLayoutError::take_too_large:
        return "take_too_large";
    case RecordingLayoutError::truncated_content:
        return "truncated_content";
    case RecordingLayoutError::invalid_frame_count:
        return "invalid_frame_count";
    case RecordingLayoutError::empty_initial_state:
        return "empty_initial_state";
    case RecordingLayoutError::initial_state_too_large:
        return "initial_state_too_large";
    case RecordingLayoutError::empty_compressed_state:
        return "empty_compressed_state";
    }
    return "unknown_recording_layout_error";
}

} // namespace ayther::audio_qa
