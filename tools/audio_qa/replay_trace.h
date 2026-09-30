#pragma once

#include "audio_chunk.h"
#include "fact_model.h"
#include "replay_execution_result.h"

#include <ayther/engine/audio_observer.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

class ReplayTraceCollector final {
  public:
    explicit ReplayTraceCollector(std::string run_id);

    void consume(std::string_view run_id, const engine::audio_observation::FactView &fact) noexcept;
    void consume(std::string_view run_id, const engine::audio_observation::PcmView &pcm) noexcept;
    [[nodiscard]] ReplayTraceSummary summarize(bool bridge_loss_free) const;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const std::vector<Fact> &facts() const noexcept;
    [[nodiscard]] const std::vector<AudioChunk> &audio_chunks() const noexcept;

  private:
    struct Node {
        ReplayTraceFactId id;
        std::string kind;
        std::vector<ReplayTraceFactId> causes;
        std::uint64_t occurrence{};
    };

    std::string run_id_;
    std::vector<Node> facts_;
    std::vector<Fact> durable_facts_;
    std::vector<AudioChunk> audio_chunks_;
    bool valid_{true};
};

[[nodiscard]] std::optional<Fact>
copy_replay_trace_fact(std::string_view run_id,
                       const engine::audio_observation::FactView &fact) noexcept;
[[nodiscard]] std::optional<AudioChunk>
copy_replay_trace_pcm(std::string_view run_id,
                      const engine::audio_observation::PcmView &pcm) noexcept;

void consume_replay_trace_fact(void *context, std::string_view run_id,
                               const engine::audio_observation::FactView &fact) noexcept;
void consume_replay_trace_pcm(void *context, std::string_view run_id,
                              const engine::audio_observation::PcmView &pcm) noexcept;

} // namespace ayther::audio_qa
