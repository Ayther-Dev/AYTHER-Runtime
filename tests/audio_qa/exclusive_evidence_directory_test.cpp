#include "exclusive_evidence_directory.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write(const std::filesystem::path &path, const std::string &content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "fixture_open_failed");
    output << content;
    require(static_cast<bool>(output), "fixture_write_failed");
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "fixture_read_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-136-exclusive-evidence";
    const auto evidence_root = fixture / "runs";
    remove_tree(fixture);
    try {
        const auto first = qa::create_exclusive_evidence_directory(evidence_root, "run-001");
        const auto *reserved = std::get_if<qa::ExclusiveEvidenceDirectory>(&first);
        require(reserved != nullptr && reserved->run_id() == "run-001" &&
                    reserved->path() == evidence_root / "run-001" &&
                    std::filesystem::is_directory(reserved->path()),
                "first_execution_directory_was_not_reserved");
        write(reserved->path() / "sentinel", "original-evidence");

        const auto collision = qa::create_exclusive_evidence_directory(evidence_root, "run-001");
        const auto *rejected = std::get_if<qa::EvidenceDirectoryCollision>(&collision);
        require(rejected != nullptr &&
                    rejected->evidence_result == qa::EvidenceResult::incomplete &&
                    rejected->diagnostic.code == "evidence_directory_collision" &&
                    rejected->diagnostic.scope == qa::DiagnosticScope::storage &&
                    rejected->diagnostic.severity == qa::DiagnosticSeverity::error &&
                    rejected->diagnostic.subject_id == "run-001" &&
                    rejected->diagnostic.stage == "evidence_directory_reservation" &&
                    qa::well_formed(rejected->diagnostic) &&
                    read(reserved->path() / "sentinel") == "original-evidence",
                "collision_overwrote_or_lost_existing_evidence");

        const auto second = qa::create_exclusive_evidence_directory(evidence_root, "run-002");
        const auto *second_reserved = std::get_if<qa::ExclusiveEvidenceDirectory>(&second);
        require(second_reserved != nullptr && second_reserved->path() != reserved->path() &&
                    std::filesystem::is_directory(second_reserved->path()),
                "independent_run_did_not_receive_exclusive_directory");

        const auto traversal = qa::create_exclusive_evidence_directory(evidence_root, "../escape");
        require(std::get_if<qa::EvidenceDirectoryError>(&traversal) != nullptr &&
                    std::get<qa::EvidenceDirectoryError>(traversal) ==
                        qa::EvidenceDirectoryError::invalid_run_id &&
                    !std::filesystem::exists(fixture / "escape"),
                "invalid_run_id_escaped_evidence_root");

        remove_tree(fixture);
        std::puts("exclusive_evidence_directory_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "exclusive_evidence_directory_test: %s\n", error.what());
        return 1;
    }
}
