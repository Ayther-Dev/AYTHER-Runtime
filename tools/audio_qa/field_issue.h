#pragma once

#include <string>

namespace ayther::audio_qa {

// Spec 002 (contracts.md C5): a problem found before admission, named by the option
// that caused it, so the CLI and the launcher can point at it (RF-1.7, RF-2.2).
struct FieldIssue {
    std::string field;
    std::string code;
    bool operator==(const FieldIssue &) const = default;
};

} // namespace ayther::audio_qa
