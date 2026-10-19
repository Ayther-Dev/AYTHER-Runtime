#pragma once

#include "fact_model.h"
#include "inspection_fact_builder.h"
#include "replay_execution_result.h"

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace ayther::audio_qa {

class ReplayFactTraceAccumulator final {
  public:
    explicit ReplayFactTraceAccumulator(std::string run_id);
    ~ReplayFactTraceAccumulator();
    ReplayFactTraceAccumulator(const ReplayFactTraceAccumulator &) = delete;
    ReplayFactTraceAccumulator &operator=(const ReplayFactTraceAccumulator &) = delete;
    ReplayFactTraceAccumulator(ReplayFactTraceAccumulator &&) noexcept;
    ReplayFactTraceAccumulator &operator=(ReplayFactTraceAccumulator &&) noexcept;

    [[nodiscard]] bool consume(const Fact &fact) noexcept;
    [[nodiscard]] ReplayTraceSummary summarize(bool transport_loss_free) const noexcept;
    // Spec 006 (RF-4.2): the run's `timeline_compiled` fact, when it replayed a timeline.
    [[nodiscard]] std::optional<TimelineCompiledRecord> timeline() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] ReplayTraceSummary summarize_replay_facts(std::span<const Fact> facts,
                                                        bool loss_free) noexcept;

} // namespace ayther::audio_qa
