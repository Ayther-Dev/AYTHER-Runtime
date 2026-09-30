#pragma once

#include "model.h"

#include <cstddef>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_registered_requests = 1024;

enum class RequestRegistrationDecision {
    accepted,
    known,
    identity_conflict,
    invalid,
    capacity_exceeded,
};

struct RequestRegistrationResult {
    RequestRegistrationDecision decision{RequestRegistrationDecision::invalid};
    Run run;
};

class RequestRegistry final {
  public:
    RequestRegistry();

    [[nodiscard]] RequestRegistrationResult register_request(Request request, Run run);
    [[nodiscard]] bool update_run(const Run &run);
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Entry {
        Request request;
        Run run;
    };

    std::vector<Entry> entries_;
};

} // namespace ayther::audio_qa
