#include "check_admission.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

qa::RequestLedger open_ledger(const std::filesystem::path &root) {
    auto result = qa::open_request_ledger(root);
    auto *ledger = std::get_if<qa::RequestLedger>(&result);
    require(ledger != nullptr, "ledger_open_failed");
    return std::move(*ledger);
}

qa::EffectiveRequest options(const std::filesystem::path &root) {
    qa::EffectiveRequest value;
    value.runtime = "runtime.exe";
    value.rom = "game.md";
    value.core = "core.dll";
    value.reference = "reference.toml";
    value.play_manifest = "play.toml";
    value.pack = "pack.ay";
    value.pack_mode = "hd";
    value.takes = {"main.ayr"};
    value.output = root.string();
    value.presentation = "none";
    return value;
}

// The pinned ROM of the request; `fill` stands for its content.
std::vector<qa::MaterialPin> pins(std::uint8_t fill) {
    qa::MaterialPin rom;
    rom.role = qa::MaterialPinRole::rom;
    rom.field = "--rom";
    rom.path = "game.md";
    rom.content.sha256.fill(fill);
    rom.content.byte_size = 16;
    return {rom};
}

const qa::CheckAdmissionResult &admission(const qa::CheckAdmissionOutcome &value) {
    const auto *result = std::get_if<qa::CheckAdmissionResult>(&value);
    require(result != nullptr, "ledger_error_was_not_expected");
    return *result;
}

} // namespace

int main() {
    const auto root = std::filesystem::current_path() / "qa-163-admission";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    try {
        require(std::filesystem::create_directory(root), "fixture_create_failed");
        auto ledger = open_ledger(root);
        qa::SessionOccupancy occupancy;
        auto first_options = options(root);
        const auto first_pins = pins(1);
        const auto first = qa::make_check_request(first_options, first_pins, "request-a", "run-a");
        const auto accepted = qa::admit_check_request(ledger, occupancy, first);
        require(admission(accepted).decision == qa::CheckAdmissionDecision::accepted &&
                    ledger.size() == 1 && occupancy.occupied(),
                "first_request_was_not_admitted");

        const auto resent = qa::admit_check_request(ledger, occupancy, first);
        require(admission(resent).decision == qa::CheckAdmissionDecision::known &&
                    admission(resent).run.run_id == "run-a" && ledger.size() == 1,
                "identical_resend_was_not_idempotent");

        auto changed_options = first_options;
        changed_options.presentation = "visible";
        const auto changed_presentation =
            qa::make_check_request(changed_options, first_pins, "request-a", "run-visible");
        require(changed_presentation.request.conditions_id != first.request.conditions_id,
                "presentation_mode_did_not_change_request_identity");
        // Spec 002 (RF-2.3, BR-059): the same paths with another content are another request.
        const auto changed_content =
            qa::make_check_request(first_options, pins(2), "request-a", "run-changed-content");
        require(changed_content.request.conditions_id != first.request.conditions_id,
                "material_content_did_not_change_request_identity");
        require(admission(qa::admit_check_request(ledger, occupancy, changed_content)).decision ==
                        qa::CheckAdmissionDecision::identity_conflict &&
                    ledger.size() == 1,
                "changed_material_content_was_not_a_conflict");
        changed_options = first_options;
        changed_options.pack = "changed.ay";
        const auto conflict = qa::admit_check_request(
            ledger, occupancy,
            qa::make_check_request(changed_options, first_pins, "request-a", "run-conflict"));
        require(admission(conflict).decision == qa::CheckAdmissionDecision::identity_conflict &&
                    ledger.size() == 1,
                "identity_conflict_changed_ledger");

        const auto busy = qa::admit_check_request(
            ledger, occupancy,
            qa::make_check_request(first_options, first_pins, "request-b", "run-b"));
        require(admission(busy).decision == qa::CheckAdmissionDecision::busy &&
                    admission(busy).active_request_id == "request-a" &&
                    admission(busy).active_run_id == "run-a" && ledger.size() == 1,
                "new_request_was_not_rejected_as_busy");

        auto closed = admission(accepted).run;
        closed.phase = qa::Phase::closed;
        closed.playback_result = qa::PlaybackResult::natural_end;
        closed.evidence_result = qa::EvidenceResult::complete;
        closed.cessation_confirmed = true;
        const auto updated = ledger.update_run(closed);
        require(std::get_if<bool>(&updated) != nullptr && std::get<bool>(updated) &&
                    occupancy.observe(closed) && !occupancy.occupied(),
                "closed_run_did_not_release_session");

        const auto repetition = qa::admit_check_request(
            ledger, occupancy,
            qa::make_check_request(first_options, first_pins, "request-b", "run-b"));
        require(admission(repetition).decision == qa::CheckAdmissionDecision::accepted &&
                    ledger.size() == 2,
                "new_identity_did_not_create_independent_repetition");

        auto reopened = open_ledger(root);
        qa::SessionOccupancy restored;
        require(qa::restore_check_occupancy(reopened, restored) &&
                    restored.active_request_id() == "request-b" &&
                    restored.active_run_id() == "run-b",
                "active_session_was_not_restored_from_ledger");
        const auto reopened_resend =
            qa::admit_check_request(reopened, restored,
                                    qa::make_check_request(first_options, first_pins, "request-b",
                                                           "different-proposed-run"));
        require(admission(reopened_resend).decision == qa::CheckAdmissionDecision::known &&
                    admission(reopened_resend).run.run_id == "run-b",
                "durable_resend_did_not_recover_known_run");

        std::filesystem::remove_all(root, ignored);
        return 0;
    } catch (...) {
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
