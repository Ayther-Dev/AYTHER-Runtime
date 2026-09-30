#pragma once

#include "model.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ayther::audio_qa {

struct TakeProcessStart {
    std::size_t take_index{};
    Run run;
    std::string process_instance_id;
};

class TakeProcessSequence final {
  public:
    explicit TakeProcessSequence(std::vector<Run> runs);

    [[nodiscard]] std::optional<TakeProcessStart> start_next(std::string process_instance_id);
    [[nodiscard]] bool finish_current(const Run &run) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t completed_count() const noexcept;
    [[nodiscard]] const std::vector<Run> &runs() const noexcept;

  private:
    std::vector<Run> runs_;
    std::vector<std::string> process_instance_ids_;
    std::size_t current_index_{};
    std::optional<std::string> active_process_instance_id_;
};

} // namespace ayther::audio_qa
