#pragma once

#include "reference_model.h"

#include <string>
#include <variant>

namespace ayther::audio_qa {

struct LockedRuntimeExecutable {
    Recorded<std::string> resolved_locator;
    BuildReference build;
    bool operator==(const LockedRuntimeExecutable &) const = default;
};

struct ConsumedRuntimeExecutable {
    Recorded<std::string> configured_locator;
    Recorded<ContentIdentity> artifact;
    bool operator==(const ConsumedRuntimeExecutable &) const = default;
};

enum class RuntimeReferenceMatch {
    verified_same,
    different_locator,
    different_content,
    unverified,
};

struct RuntimeReferenceComparison {
    RuntimeReferenceMatch match{RuntimeReferenceMatch::unverified};
    Reference initial;
    Reference effective;
    Material consumed_executable;
    bool operator==(const RuntimeReferenceComparison &) const = default;
};

enum class RuntimeReferenceComparisonError {
    invalid_initial_reference,
    invalid_locked_executable,
    invalid_consumed_executable,
    invalid_effective_reference,
};

using RuntimeReferenceComparisonResult =
    std::variant<RuntimeReferenceComparison, RuntimeReferenceComparisonError>;

[[nodiscard]] RuntimeReferenceComparisonResult
compare_runtime_reference(Reference initial, const LockedRuntimeExecutable &locked,
                          const ConsumedRuntimeExecutable &consumed,
                          std::string execution_reference_id, Provenance comparison_provenance);

} // namespace ayther::audio_qa
