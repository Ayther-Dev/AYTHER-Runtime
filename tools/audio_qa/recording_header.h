#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t recording_fixed_header_bytes = 8;
inline constexpr std::uint32_t oldest_supported_recording_version = 2;
// .arp v9: the take ends with the detector's FM register shadow at its start (Engine
// `AytherRecording::audio_fm_regs`): u32 size (0 = none, or 512) + bytes.
// .arp v10 (Engine spec 007) appends the take id (u64) after it. Whether the
// Runtime's Engine can LOAD a v10 take is negotiated: a request with one needs
// `take_format_v10`.
inline constexpr std::uint32_t current_recording_version = 10;
inline constexpr std::uint32_t recording_fm_registers_version = 9;
inline constexpr std::uint32_t recording_fm_register_bytes = 0x200;
inline constexpr std::uint32_t take_id_recording_version = 10;
inline constexpr std::uint32_t take_id_bytes = 8;
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

enum class RecordingFmRegistersError {
    none,
    invalid_layout,
    truncated_content,
    invalid_section,
    invalid_size,
    trailing_content,
};

struct RecordingFmRegistersResult {
    RecordingFmRegistersError error{RecordingFmRegistersError::none};
    // Empty when the take carries no shadow: older than v9, or recorded without a detector.
    std::vector<std::uint8_t> registers;
};

// The FM register shadow of a v9 take, read the way the Engine loader reads it: every section
// the version declares is walked (v2 trim and stats, v3 sprite history, v4 keyframes, v7 audio
// history, v8 hash algorithm) and the take must end right after the shadow. A take older than
// v9 has none and decodes as empty without error.
[[nodiscard]] RecordingFmRegistersResult
decode_recording_fm_registers(std::span<const std::byte> recording, const RecordingLayout &layout);

[[nodiscard]] std::string_view
recording_fm_registers_error_code(RecordingFmRegistersError error) noexcept;

} // namespace ayther::audio_qa
