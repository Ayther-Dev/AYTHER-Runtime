#pragma once

#include <cstdint>

namespace ayther::runtime::allocation_metrics {
struct Snapshot {
  std::uint64_t calls;
  std::uint64_t bytes;
};
Snapshot snapshot() noexcept;
} // namespace ayther::runtime::allocation_metrics

