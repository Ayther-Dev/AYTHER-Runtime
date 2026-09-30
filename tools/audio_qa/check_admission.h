#pragma once

#include "check_options.h"
#include "check_profile.h"
#include "request_ledger.h"
#include "session_occupancy.h"

#include <string>
#include <variant>

namespace ayther::audio_qa {

struct CheckRequestDraft {
    Request request;
    Run run;
};

enum class CheckAdmissionDecision {
    accepted,
    known,
    identity_conflict,
    busy,
    invalid,
    capacity_exceeded,
};

struct CheckAdmissionResult {
    CheckAdmissionDecision decision{CheckAdmissionDecision::invalid};
    Run run;
    std::string active_request_id;
    std::string active_run_id;
};

using CheckAdmissionOutcome = std::variant<CheckAdmissionResult, RequestLedgerError>;

[[nodiscard]] CheckRequestDraft make_check_request(const CheckOptions &options,
                                                   const TakeSelection &selection,
                                                   std::string request_id, std::string run_id);
[[nodiscard]] bool restore_check_occupancy(const RequestLedger &ledger,
                                           SessionOccupancy &occupancy);
[[nodiscard]] CheckAdmissionOutcome admit_check_request(RequestLedger &ledger,
                                                        SessionOccupancy &occupancy,
                                                        CheckRequestDraft draft) noexcept;
[[nodiscard]] std::string generate_check_id(std::string_view prefix);

} // namespace ayther::audio_qa
