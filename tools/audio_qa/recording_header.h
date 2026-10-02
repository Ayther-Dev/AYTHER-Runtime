#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ayther::audio_qa {

inline constexpr std::size_t recording_fixed_header_bytes = 8;
inline constexpr std::uint32_t oldest_supported_recording_version = 2;
inline constexpr std::uint32_t current_recording_version = 8;
inline constexpr std::uint64_t max_recording_bytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t max_recording_state_bytes = 64U * 1024U * 1024U;
inline constexpr std::uint32_t min_recording_frames = 1;
inline constexpr std::uint32_t max_recording_frames = 54'000;

enum class RecordingHeaderError {
    none,
    empty_take,
    truncated_header,
    invalid_magic,
    unsupported_version,
};

struct RecordingHeader {
    std::uint32_t version{};
};

struct RecordingHeaderResult {
    RecordingHeaderError error{RecordingHeaderError::none};
    RecordingHeader header{};
};

struct RecordingByteRange {
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct RecordingLayout {
    RecordingHeader header{};
    RecordingByteRange game_id{};
    RecordingByteRange name{};
    std::uint32_t frame_count{};
    std::uint32_t raw_state_bytes{};
    RecordingByteRange compressed_state{};
    RecordingByteRange inputs{};
};

enum class RecordingLayoutError {
    none,
    invalid_header,
    take_too_large,
    truncated_content,
    invalid_frame_count,
    empty_initial_state,
    initial_state_too_large,
    empty_compressed_state,
};

struct RecordingLayoutResult {
    RecordingLayoutError error{RecordingLayoutError::none};
    RecordingHeaderError header_error{RecordingHeaderError::none};
    RecordingLayout layout{};
};

[[nodiscard]] RecordingHeaderResult
decode_recording_header(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::string_view recording_header_error_code(RecordingHeaderError error) noexcept;

[[nodiscard]] RecordingLayoutResult
decode_recording_layout(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] RecordingLayoutResult decode_recording_layout(std::span<const std::byte> header_bytes,
                                                            std::uint64_t total_bytes) noexcept;

[[nodiscard]] std::string_view recording_layout_error_code(RecordingLayoutError error) noexcept;

} // namespace ayther::audio_qa
