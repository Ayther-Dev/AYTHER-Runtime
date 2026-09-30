#pragma once

#include "model.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace ayther::audio_qa {

enum class SessionAdmissionDecision {
    accepted,
    busy,
    invalid_request,
};

struct SessionAdmissionResult {
    SessionAdmissionDecision decision{SessionAdmissionDecision::invalid_request};
    std::string_view run_id;
    std::string_view active_request_id;
    std::string_view active_run_id;
};

class SessionOccupancy final {
  public:
    [[nodiscard]] SessionAdmissionResult admit(const Request &request, std::string run_id);
    [[nodiscard]] bool observe(const Run &run) noexcept;

    [[nodiscard]] bool occupied() const noexcept;
    [[nodiscard]] std::string_view active_request_id() const noexcept;
    [[nodiscard]] std::string_view active_run_id() const noexcept;
    [[nodiscard]] std::size_t queued_requests() const noexcept;

  private:
    std::string request_id_;
    std::string run_id_;
};

} // namespace ayther::audio_qa
