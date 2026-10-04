#pragma once

#include "check_options.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

// Spec 002 (RF-1.5, RF-1.8; contracts.md C5): the single source of the `check`
// options. The parser, the usage text, the `options` inventory and the launcher
// fields are all derived from this table, so they cannot drift apart.
enum class CheckOptionCategory {
    rom,
    take,
    pack,
    environment,
    auxiliary,
    destination,
    presentation,
    language,
};

enum class CheckOptionKind { text, path, choice, unsigned_integer, key_value };

// How the option relates to the reference version (v0.1.0-beta.8). Anything other
// than `same` must cite the requirements that justify the change (RF-1.8).
enum class ReferenceCorrespondence { same, changed, added };

struct CheckOptionDescriptor {
    std::string_view flag;
    CheckOptionCategory category{CheckOptionCategory::environment};
    CheckOptionKind kind{CheckOptionKind::text};
    bool required{};
    bool repeatable{};
    std::string_view default_value;
    std::span<const std::string_view> values;
    CheckOptionErrorCode invalid_value_error{CheckOptionErrorCode::unknown_option};
    std::string CheckOptions::*text_field{};
    std::vector<std::string> CheckOptions::*list_field{};
    std::string_view launcher_field;
    ReferenceCorrespondence correspondence{ReferenceCorrespondence::same};
    std::string_view requirements;
};

// Options of the subcommands that spec 002 does not change. They only feed the
// published inventory, which keeps the reference schema.
struct FixedOptionDescriptor {
    std::string_view command;
    std::string_view flag;
    bool required{};
    std::string_view default_value;
    std::span<const std::string_view> values;
};

[[nodiscard]] std::span<const CheckOptionDescriptor> check_option_descriptors() noexcept;
[[nodiscard]] std::span<const FixedOptionDescriptor> fixed_option_descriptors() noexcept;
[[nodiscard]] const CheckOptionDescriptor *find_check_option(std::string_view flag) noexcept;
[[nodiscard]] std::string_view check_option_category_code(CheckOptionCategory category) noexcept;
[[nodiscard]] std::string_view
reference_correspondence_code(ReferenceCorrespondence correspondence) noexcept;

// TOML inventory with the schema of evidence/rf1-option-inventory-beta8.toml.
[[nodiscard]] std::string format_check_option_inventory(std::string_view ref,
                                                        std::string_view commit);
// One line per option, for `audio_qa_usage:` diagnostics.
[[nodiscard]] std::string format_check_usage();

} // namespace ayther::audio_qa
