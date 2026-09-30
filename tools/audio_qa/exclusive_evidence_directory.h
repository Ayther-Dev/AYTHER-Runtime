#pragma once

#include "coverage_model.h"
#include "model.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

class ExclusiveEvidenceDirectory final {
  public:
    ExclusiveEvidenceDirectory(std::filesystem::path path, std::string run_id);

    [[nodiscard]] const std::filesystem::path &path() const noexcept;
    [[nodiscard]] const std::string &run_id() const noexcept;

  private:
    std::filesystem::path path_;
    std::string run_id_;
};

struct EvidenceDirectoryCollision {
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    Diagnostic diagnostic;
};

enum class EvidenceDirectoryError { invalid_root, invalid_run_id, io_error };

using ExclusiveEvidenceDirectoryResult =
    std::variant<ExclusiveEvidenceDirectory, EvidenceDirectoryCollision, EvidenceDirectoryError>;

[[nodiscard]] ExclusiveEvidenceDirectoryResult
create_exclusive_evidence_directory(const std::filesystem::path &evidence_root,
                                    std::string_view run_id) noexcept;

} // namespace ayther::audio_qa
