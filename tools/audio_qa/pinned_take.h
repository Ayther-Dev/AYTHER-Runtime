#pragma once

#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::uint64_t max_pinned_take_bytes = 64U * 1024U * 1024U;

class PinnedTake final {
  public:
    PinnedTake(std::filesystem::path source_path, std::vector<std::byte> bytes,
               ContentIdentity identity);

    [[nodiscard]] const std::filesystem::path &source_path() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] const ContentIdentity &identity() const noexcept;

  private:
    std::filesystem::path source_path_;
    std::vector<std::byte> bytes_;
    ContentIdentity identity_;
};

enum class PinnedTakeError {
    invalid_path,
    empty_input,
    input_too_large,
    io_error,
    identity_mismatch,
};

using PinnedTakeResult = std::variant<PinnedTake, PinnedTakeError>;

[[nodiscard]] PinnedTakeResult
open_pinned_take(const std::filesystem::path &path,
                 std::optional<ContentIdentity> expected = std::nullopt) noexcept;

} // namespace ayther::audio_qa
