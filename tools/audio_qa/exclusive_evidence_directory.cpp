#include "exclusive_evidence_directory.h"

#include "long_path.h"
#include "model_limits.h"

#include <system_error>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool valid_run_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > max_identity_bytes || value == "." || value == "..") {
        return false;
    }
    for (const unsigned char character : value) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                                  (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        if (!alphanumeric && character != '-' && character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

EvidenceDirectoryCollision collision(const std::string_view run_id) {
    Diagnostic diagnostic;
    diagnostic.code = "evidence_directory_collision";
    diagnostic.scope = DiagnosticScope::storage;
    diagnostic.severity = DiagnosticSeverity::error;
    diagnostic.subject_id = run_id;
    diagnostic.stage = "evidence_directory_reservation";
    diagnostic.affected_samples = {Availability::not_applicable, std::nullopt, "no_audio_consumed"};
    diagnostic.last_confirmed_frame = {Availability::unknown, std::nullopt, "replay_not_started"};
    diagnostic.detail = "exclusive evidence directory already exists; existing "
                        "contents were preserved";
    return {EvidenceResult::incomplete, std::move(diagnostic)};
}

} // namespace

ExclusiveEvidenceDirectory::ExclusiveEvidenceDirectory(std::filesystem::path path,
                                                       std::string run_id)
    : path_(std::move(path)), run_id_(std::move(run_id)) {}

const std::filesystem::path &ExclusiveEvidenceDirectory::path() const noexcept { return path_; }

const std::string &ExclusiveEvidenceDirectory::run_id() const noexcept { return run_id_; }

ExclusiveEvidenceDirectoryResult
create_exclusive_evidence_directory(const std::filesystem::path &evidence_root,
                                    const std::string_view run_id) noexcept {
    try {
        if (evidence_root.empty()) {
            return EvidenceDirectoryError::invalid_root;
        }
        if (!valid_run_id(run_id)) {
            return EvidenceDirectoryError::invalid_run_id;
        }

        // D-12: the directories are created through their extended form; the evidence
        // directory keeps the path the caller gave.
        const auto native_root = long_path(evidence_root);
        std::error_code error;
        std::filesystem::create_directories(native_root, error);
        if (error || !std::filesystem::is_directory(native_root, error) || error) {
            return EvidenceDirectoryError::io_error;
        }

        const auto execution_path = evidence_root / std::string{run_id};
        const auto native_execution = native_root / std::string{run_id};
        if (std::filesystem::create_directory(native_execution, error)) {
            return ExclusiveEvidenceDirectory{execution_path, std::string{run_id}};
        }

        std::error_code existence_error;
        if (std::filesystem::exists(native_execution, existence_error) && !existence_error) {
            auto result = collision(run_id);
            if (!well_formed(result.diagnostic)) {
                return EvidenceDirectoryError::io_error;
            }
            return result;
        }
        return EvidenceDirectoryError::io_error;
    } catch (...) {
        return EvidenceDirectoryError::io_error;
    }
}

} // namespace ayther::audio_qa
