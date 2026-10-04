#include "check_admission.h"

#include "content_hash.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string_view>
#include <utility>

namespace ayther::audio_qa {

namespace {

void hash_text(ContentHasher &hasher, std::string_view text) noexcept {
    hasher.update(std::as_bytes(std::span{text.data(), text.size()}));
    constexpr std::array separator{std::byte{0}};
    hasher.update(separator);
}

std::string hexadecimal(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4U];
        result[index * 2 + 1] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

CheckAdmissionResult result(CheckAdmissionDecision decision, Run run = {}) {
    return {decision, std::move(run), {}, {}};
}

} // namespace

std::string check_conditions_id(const EffectiveRequest &effective,
                                std::span<const MaterialPin> pins) {
    ContentHasher hasher;
    hash_text(hasher, effective.runtime);
    hash_text(hasher, effective.rom);
    hash_text(hasher, effective.core);
    hash_text(hasher, effective.pack.value_or(std::string{}));
    hash_text(hasher, effective.pack ? "pack" : "no-pack");
    hash_text(hasher, effective.pack_mode);
    hash_text(hasher, effective.trust_registry);
    hash_text(hasher, effective.presentation);
    hash_text(hasher, effective.play_manifest.value_or(std::string{}));
    hash_text(hasher, effective.reference.value_or(std::string{}));
    const auto &conditions = effective.conditions;
    for (const auto *value : {&conditions.profile, &conditions.subsystems, &conditions.mute_buses,
                              &conditions.video_output, &conditions.patch})
        hash_text(hasher, value->value_or(std::string{}));
    hash_text(hasher, !conditions.shaders ? "" : (*conditions.shaders ? "on" : "off"));
    for (const auto &option : conditions.core_options)
        hash_text(hasher, option);
    hash_text(hasher, "takes");
    for (const auto &take : effective.takes)
        hash_text(hasher, take);
    hash_text(hasher, "materials");
    for (const auto &pin : pins) {
        hash_text(hasher, material_pin_role_code(pin.role));
        hash_text(hasher, pin.field);
        hash_text(hasher, hexadecimal(pin.content.sha256));
        hash_text(hasher, std::to_string(pin.content.byte_size));
    }
    const auto identity = hasher.finish();
    return "conditions-sha256-" + hexadecimal(identity.sha256);
}

CheckRequestDraft make_check_request(const EffectiveRequest &effective,
                                     std::span<const MaterialPin> pins, std::string request_id,
                                     std::string run_id) {
    Request request{std::move(request_id), "audio-qa-check-v1",
                    check_conditions_id(effective, pins), effective.takes, Admission::pending};
    Run run;
    run.run_id = std::move(run_id);
    run.request_id = request.request_id;
    if (!effective.takes.empty())
        run.take_id = effective.takes.front();
    return {std::move(request), std::move(run)};
}

bool restore_check_occupancy(const RequestLedger &ledger, SessionOccupancy &occupancy) {
    for (const auto &entry : ledger.entries()) {
        // Spec 002 (RNF-5): a closed run no longer occupies the destination. One whose
        // cessation could not be confirmed was closed by the supervisor that held the
        // destination lock, or recovered after an interruption.
        if (entry.run.phase == Phase::closed)
            continue;
        if (occupancy.occupied() ||
            occupancy.admit(entry.request, entry.run.run_id).decision !=
                SessionAdmissionDecision::accepted ||
            !occupancy.observe(entry.run))
            return false;
    }
    return true;
}

CheckAdmissionOutcome admit_check_request(RequestLedger &ledger, SessionOccupancy &occupancy,
                                          CheckRequestDraft draft) noexcept {
    try {
        const auto inspected = ledger.inspect_request(draft.request, draft.run);
        switch (inspected.decision) {
        case RequestRegistrationDecision::known:
            return result(CheckAdmissionDecision::known, inspected.run);
        case RequestRegistrationDecision::identity_conflict:
            return result(CheckAdmissionDecision::identity_conflict, inspected.run);
        case RequestRegistrationDecision::invalid:
            return result(CheckAdmissionDecision::invalid);
        case RequestRegistrationDecision::capacity_exceeded:
            return result(CheckAdmissionDecision::capacity_exceeded);
        case RequestRegistrationDecision::accepted:
            break;
        }

        if (occupancy.occupied()) {
            CheckAdmissionResult busy = result(CheckAdmissionDecision::busy);
            busy.active_request_id = std::string{occupancy.active_request_id()};
            busy.active_run_id = std::string{occupancy.active_run_id()};
            return busy;
        }

        const auto request = draft.request;
        const auto run_id = draft.run.run_id;
        const auto registered =
            ledger.register_request(std::move(draft.request), std::move(draft.run));
        if (const auto *error = std::get_if<RequestLedgerError>(&registered))
            return *error;
        const auto &registration = std::get<RequestRegistrationResult>(registered);
        if (registration.decision != RequestRegistrationDecision::accepted)
            return result(CheckAdmissionDecision::invalid);
        if (occupancy.admit(request, run_id).decision != SessionAdmissionDecision::accepted)
            return RequestLedgerError::io_error;
        return result(CheckAdmissionDecision::accepted, registration.run);
    } catch (...) {
        return RequestLedgerError::io_error;
    }
}

std::string generate_check_id(std::string_view prefix) {
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::array<std::uint64_t, 2> entropy{ticks, ticks ^ 0x9e3779b97f4a7c15ULL};
    try {
        std::random_device random;
        entropy[0] ^= (static_cast<std::uint64_t>(random()) << 32U) ^ random();
        entropy[1] ^= (static_cast<std::uint64_t>(random()) << 32U) ^ random();
    } catch (...) {
    }
    return std::string{prefix} +
           hexadecimal({reinterpret_cast<const std::uint8_t *>(entropy.data()), sizeof(entropy)});
}

} // namespace ayther::audio_qa
