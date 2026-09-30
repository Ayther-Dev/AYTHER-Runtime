#pragma once

#include "run_lifecycle.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

enum class EvidenceCloseFailure {
    none,
    fact_drain_failed,
    pcm_drain_failed,
    durable_write_failed,
};

struct EvidenceCloseReport {
    EvidenceCloseFailure failure{EvidenceCloseFailure::none};
    std::optional<std::uint64_t> last_durable_sample;
    bool operator==(const EvidenceCloseReport &) const = default;
};

class EvidenceClose final {
  public:
    [[nodiscard]] bool finish(EvidenceCloseReport report, RunLifecycle &lifecycle) noexcept;
    [[nodiscard]] const std::optional<EvidenceCloseReport> &report() const noexcept;

  private:
    std::optional<EvidenceCloseReport> report_;
};

} // namespace ayther::audio_qa
