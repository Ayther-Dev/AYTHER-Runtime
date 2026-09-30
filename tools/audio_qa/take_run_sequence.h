#pragma once

#include "model.h"

#include <vector>

namespace ayther::audio_qa {

enum class TakeRunSequenceError {
    none,
    invalid_request,
    run_count_mismatch,
    duplicate_take,
    duplicate_run,
};

class TakeRunSequence final {
  public:
    TakeRunSequence(Request request, std::vector<std::string> run_ids);

    [[nodiscard]] TakeRunSequenceError error() const noexcept;
    [[nodiscard]] const std::vector<Run> &runs() const noexcept;
    [[nodiscard]] bool update(const Run &run) noexcept;

  private:
    TakeRunSequenceError error_{TakeRunSequenceError::none};
    std::vector<Run> runs_;
};

} // namespace ayther::audio_qa
