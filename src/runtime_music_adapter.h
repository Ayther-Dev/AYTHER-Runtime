#pragma once

#include <ayther/engine/music_execution_adapter.hpp>

namespace runtime {

class MusicRuntimeAdapter {
public:
  explicit MusicRuntimeAdapter(
      const ayther::engine::MusicSequenceDefinition &definition) noexcept
      : policy_(definition) {}
  [[nodiscard]] bool start(ayther::engine::OccurrenceId occurrence) {
    return policy_.start(occurrence);
  }
  [[nodiscard]] ayther::engine::MusicExecutionAdapter &policy() noexcept {
    return policy_;
  }
private:
  ayther::engine::MusicExecutionAdapter policy_;
};

} // namespace runtime
