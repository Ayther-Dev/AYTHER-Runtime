#pragma once

#include "effective_values.h"
#include "material_preflight.h"
#include "request_ledger.h"
#include "session_occupancy.h"

#include <span>
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

// Spec 002 (RF-2.3, plan §5.2): the identity of the conditions covers the effective
// values and the pinned content of every material, so the same paths with another
// content are another request.
[[nodiscard]] CheckRequestDraft make_check_request(const EffectiveRequest &effective,
                                                   std::span<const MaterialPin> pins,
                                                   std::string request_id, std::string run_id);
[[nodiscard]] std::string check_conditions_id(const EffectiveRequest &effective,
                                              std::span<const MaterialPin> pins);
[[nodiscard]] bool restore_check_occupancy(const RequestLedger &ledger,
                                           SessionOccupancy &occupancy);
[[nodiscard]] CheckAdmissionOutcome admit_check_request(RequestLedger &ledger,
                                                        SessionOccupancy &occupancy,
                                                        CheckRequestDraft draft) noexcept;
[[nodiscard]] std::string generate_check_id(std::string_view prefix);

} // namespace ayther::audio_qa
