#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

namespace ayther::audio_qa {

enum class ExclusiveFileWriteResult { written, already_exists, io_error };

[[nodiscard]] ExclusiveFileWriteResult
write_exclusive_file(const std::filesystem::path &path, std::span<const std::byte> bytes) noexcept;

} // namespace ayther::audio_qa
