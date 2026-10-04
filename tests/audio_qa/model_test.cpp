#include "model_toml.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
std::string encoded(const qa::EncodeResult &value) {
    const auto *text = std::get_if<std::string>(&value);
    require(text != nullptr, "encoding_failed");
    return *text;
}
void requests() {
    const qa::Request request{"request-1",
                              "session-1",
                              "conditions-sha256",
                              {"take-B", "take-A", "take-B"},
                              qa::Admission::accepted,
                              std::nullopt};
    const auto decoded = qa::request_from_toml(encoded(qa::to_toml(request)));
    require(std::holds_alternative<qa::Request>(decoded) &&
                std::get<qa::Request>(decoded) == request,
            "request_order_or_identity_changed");
    auto invalid = request;
    invalid.take_ids.clear();
    require(std::holds_alternative<qa::CodecError>(qa::to_toml(invalid)), "empty_request_accepted");
    auto bounded = request;
    bounded.request_id = std::string(qa::max_identity_bytes, 'r');
    bounded.take_ids.assign(qa::max_request_takes, "take-A");
    require(std::get<qa::Request>(qa::request_from_toml(encoded(qa::to_toml(bounded)))) == bounded,
            "valid_identity_or_take_boundary_rejected");
    bounded.take_ids.push_back("take-A");
    require(std::holds_alternative<qa::CodecError>(qa::to_toml(bounded)), "take_limit_ignored");
    bounded = request;
    bounded.request_id = std::string(qa::max_identity_bytes + 1, 'r');
    require(std::holds_alternative<qa::CodecError>(qa::to_toml(bounded)), "identity_limit_ignored");
}
void runs() {
    qa::Run run{"run-1",
                "request-1",
                "take-A",
                qa::Phase::preparing,
                qa::PlaybackResult::not_started,
                qa::EvidenceResult::pending,
                qa::EquivalenceResult::unknown,
                std::nullopt,
                std::nullopt,
                false};
    const auto untouched = qa::run_from_toml(encoded(qa::to_toml(run)));
    require(std::holds_alternative<qa::Run>(untouched) && std::get<qa::Run>(untouched) == run,
            "unknown_progress_became_zero");
    run.phase = qa::Phase::closing;
    run.playback_result = qa::PlaybackResult::natural_end;
    run.evidence_result = qa::EvidenceResult::incomplete;
    run.equivalence_result = qa::EquivalenceResult::unknown;
    run.last_executed_frame = UINT64_MAX;
    run.last_durable_sample = 0;
    const auto result = qa::run_from_toml(encoded(qa::to_toml(run)));
    require(std::holds_alternative<qa::Run>(result) && std::get<qa::Run>(result) == run,
            "closing_failure_changed_playback_or_integer_precision");
    run.phase = qa::Phase::closed;
    run.cessation_confirmed = true;
    run.playback_result = qa::PlaybackResult::cancelled;
    run.equivalence_result = qa::EquivalenceResult::verified;
    require(std::get<qa::Run>(qa::run_from_toml(encoded(qa::to_toml(run)))) == run,
            "independent_results_changed");
}
void rejects() {
    require(std::get<qa::CodecError>(qa::run_from_toml("[broken")) ==
                qa::CodecError::malformed_document,
            "malformed_document_accepted");
    require(std::get<qa::CodecError>(qa::run_from_toml("schema_version=2\nschema_minor=0")) ==
                qa::CodecError::incompatible_version,
            "unknown_major_accepted");
    require(std::get<qa::CodecError>(qa::run_from_toml("schema_version=1\nschema_minor=1")) ==
                qa::CodecError::incompatible_version,
            "unknown_minor_accepted");
    const qa::Run run{"r",
                      "q",
                      "t",
                      qa::Phase::preparing,
                      qa::PlaybackResult::not_started,
                      qa::EvidenceResult::pending,
                      qa::EquivalenceResult::unknown,
                      std::nullopt,
                      std::nullopt,
                      false};
    const auto text = encoded(qa::to_toml(run));
    for (const auto *suffix :
         {"last_executed_frame = '-1'\n", "last_executed_frame = '18446744073709551616'\n",
          "last_executed_frame = 0\n", "last_executed_frame = '0.0'\n"}) {
        require(std::holds_alternative<qa::CodecError>(qa::run_from_toml(text + "\n" + suffix)),
                "invalid_unsigned_progress_accepted");
    }
    require(std::get<qa::CodecError>(qa::run_from_toml(
                std::string(qa::max_metadata_bytes + 1, ' '))) == qa::CodecError::too_large,
            "oversized_document_accepted");
    require(std::holds_alternative<qa::CodecError>(qa::request_from_toml(text)),
            "wrong_document_kind_accepted");
    require(std::holds_alternative<qa::Run>(
                qa::run_from_toml(text + std::string(qa::max_metadata_bytes - text.size(), ' '))),
            "valid_metadata_boundary_rejected");
    require(std::holds_alternative<qa::CodecError>(qa::run_from_toml(text + "\nunknown = 1\n")),
            "undeclared_extension_accepted");
    auto invalid_enum = run;
    invalid_enum.playback_result = static_cast<qa::PlaybackResult>(99);
    require(std::holds_alternative<qa::CodecError>(qa::to_toml(invalid_enum)),
            "invalid_enum_accepted");
}
} // namespace

int main(int argc, char *argv[]) {
    try {
        require(argc == 2, "expected_test_case");
        const std::string_view mode{argv[1]};
        if (mode == "request") {
            requests();
        } else if (mode == "run") {
            runs();
        } else if (mode == "reject") {
            rejects();
        } else {
            throw std::runtime_error("unknown_test_case");
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_model_test_failed: %s\n", error.what());
        return 1;
    }
}
