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

namespace {

// Forward reader over the whole take, mirroring the Engine loader's cursor.
class TailCursor {
  public:
    TailCursor(const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
        : bytes_(bytes), offset_(offset) {}

    [[nodiscard]] bool read_u32(std::uint32_t &value) noexcept {
        if (!has(sizeof(value)))
            return false;
        value = read_u32_le(bytes_, static_cast<std::size_t>(offset_));
        offset_ += sizeof(value);
        return true;
    }
    [[nodiscard]] bool skip(const std::uint64_t count) noexcept {
        if (!has(count))
            return false;
        offset_ += count;
        return true;
    }
    [[nodiscard]] bool has(const std::uint64_t count) const noexcept {
        return has_bytes(offset_, count, bytes_.size());
    }
    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return static_cast<std::size_t>(offset_); }

  private:
    std::span<const std::byte> bytes_;
    std::uint64_t offset_;
};

// v3/v7 history: u32 total, total × u64 hashes, frame_count × u32 per-frame counts (CSR).
[[nodiscard]] bool skip_hash_history(TailCursor &cursor, const std::uint32_t frames) noexcept {
    std::uint32_t total{};
    return cursor.read_u32(total) && cursor.skip(static_cast<std::uint64_t>(total) * 8U) &&
           cursor.skip(static_cast<std::uint64_t>(frames) * 4U);
}

} // namespace

RecordingFmRegistersResult decode_recording_fm_registers(const std::span<const std::byte> recording,
                                                         const RecordingLayout &layout) {
    RecordingFmRegistersResult result{};
    if (layout.header.version < recording_fm_registers_version)
        return result;
    if (layout.inputs.offset > recording.size() ||
        layout.inputs.size > recording.size() - layout.inputs.offset) {
        result.error = RecordingFmRegistersError::invalid_layout;
        return result;
    }
    const auto frames = layout.frame_count;
    TailCursor cursor{recording, layout.inputs.offset + layout.inputs.size};
    const auto truncated = [&result] {
        result.error = RecordingFmRegistersError::truncated_content;
        return result;
    };

    // v2: trim marks and the per-frame summary (v6+: six u16 per frame).
    if (!cursor.skip(8U) || !cursor.skip(static_cast<std::uint64_t>(frames) * 12U))
        return truncated();
    // v3: sprite-hash history.
    if (!skip_hash_history(cursor, frames))
        return truncated();
    // v4: baked keyframes.
    std::uint32_t keyframe_count{};
    if (!cursor.read_u32(keyframe_count))
        return truncated();
    if (keyframe_count > frames + 1U) {
        result.error = RecordingFmRegistersError::invalid_section;
        return result;
    }
    for (std::uint32_t index = 0; index < keyframe_count; ++index) {
        std::uint32_t frame{}, raw_size{}, compressed_size{};
        if (!cursor.read_u32(frame) || !cursor.read_u32(raw_size) ||
            !cursor.read_u32(compressed_size))
            return truncated();
        if (frame > frames || raw_size == 0U) {
            result.error = RecordingFmRegistersError::invalid_section;
            return result;
        }
        if (!cursor.skip(compressed_size))
            return truncated();
    }
    // v7: audio-hash history.
    if (!skip_hash_history(cursor, frames))
        return truncated();
    // v8: sprite hash algorithm.
    if (!cursor.skip(4U))
        return truncated();
    // v9: the shadow.
    std::uint32_t size{};
    if (!cursor.read_u32(size))
        return truncated();
    if (size != 0U && size != recording_fm_register_bytes) {
        result.error = RecordingFmRegistersError::invalid_size;
        return result;
    }
    if (!cursor.has(size))
        return truncated();
    const auto begin = cursor.offset();
    if (!cursor.skip(size))
        return truncated();
    // v10 (Engine spec 007): the take id follows the shadow.
    if (layout.header.version >= take_id_recording_version && !cursor.skip(take_id_bytes))
        return truncated();
    if (!cursor.at_end()) {
        result.error = RecordingFmRegistersError::trailing_content;
        return result;
    }
    result.registers.resize(size);
    for (std::size_t index = 0; index < size; ++index)
        result.registers[index] = std::to_integer<std::uint8_t>(recording[begin + index]);
    return result;
}

std::string_view recording_fm_registers_error_code(const RecordingFmRegistersError error) noexcept {
    switch (error) {
    case RecordingFmRegistersError::none:
        return "none";
    case RecordingFmRegistersError::invalid_layout:
        return "invalid_layout";
    case RecordingFmRegistersError::truncated_content:
        return "truncated_content";
    case RecordingFmRegistersError::invalid_section:
        return "invalid_section";
    case RecordingFmRegistersError::invalid_size:
        return "invalid_size";
    case RecordingFmRegistersError::trailing_content:
        return "trailing_content";
    }
    return "unknown_recording_fm_registers_error";
}

} // namespace ayther::audio_qa
