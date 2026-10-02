#pragma once

#include "reference_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ayther::audio_qa {

class ContentHasher final {
  public:
    void update(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] ContentIdentity finish() const noexcept;

  private:
    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> pending_{};
    std::size_t pending_size_{};
    std::uint64_t byte_size_{};
};

[[nodiscard]] ContentIdentity identify_content(std::span<const std::byte> bytes) noexcept;

} // namespace ayther::audio_qa
