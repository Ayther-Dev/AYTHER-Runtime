#pragma once

#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

inline constexpr std::size_t max_play_launch_manifest_bytes = 256U * 1024U;
inline constexpr std::uint32_t supported_play_launch_manifest_format = 1;

enum class PlayLaunchManifestError {
    invalid_identity,
    input_too_large,
    io_error,
    parse_error,
    unsupported_format,
    invalid_manifest,
};

using PlayLaunchManifestResult = std::variant<Reference, PlayLaunchManifestError>;

[[nodiscard]] PlayLaunchManifestResult
decode_play_launch_manifest(std::string_view text, std::string baseline_id,
                            std::string execution_reference_id, std::string evidence_id);

[[nodiscard]] PlayLaunchManifestResult load_play_launch_manifest(const std::filesystem::path &path,
                                                                 std::string baseline_id,
                                                                 std::string execution_reference_id,
                                                                 std::string evidence_id) noexcept;

} // namespace ayther::audio_qa
