#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace ayther::replay_qa_launcher {

// Spec 002, plan §5.1 (RF-1.8; BR-171): the correspondence between the replay options of the
// reference version, read from its frozen inventory, and the fields of the delivered launcher.
// Every option of the reference must be the same, changed or added by a cited requirement,
// and every option of the table must have a launcher field.
struct CorrespondenceError {
    // The option without a correspondence; empty when the inventory cannot be read.
    std::string flag;
    std::string reason;
};

// Markdown of evidence/rf1-option-correspondence.md, or the first option that fails.
[[nodiscard]] std::variant<std::string, CorrespondenceError>
format_option_correspondence(std::string_view reference_inventory);

} // namespace ayther::replay_qa_launcher
