#include "check_option_descriptors.h"

#include "model_limits.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace ayther::audio_qa {

namespace {

constexpr std::array<std::string_view, 2> language_values{"es", "en"};
constexpr std::array<std::string_view, 2> presentation_values{"none", "visible"};
constexpr std::array<std::string_view, 2> pack_mode_values{"hd", "original"};
constexpr std::array<std::string_view, 2> shaders_values{"on", "off"};

using enum CheckOptionCategory;
using enum CheckOptionKind;
using enum ReferenceCorrespondence;
constexpr auto no_error = CheckOptionErrorCode::unknown_option;

// Order matters: required options are reported in this order and choices are
// validated in this order. The first options keep the reference order.
const std::array<CheckOptionDescriptor, 20> descriptors{{
    {"--runtime",
     environment,
     path,
     true,
     false,
     {},
     {},
     no_error,
     &CheckOptions::runtime,
     nullptr,
     "runtime",
     same,
     {}},
    {"--rom",
     rom,
     path,
     true,
     false,
     {},
     {},
     no_error,
     &CheckOptions::rom,
     nullptr,
     "rom",
     added,
     "RF-1.1"},
    {"--core",
     environment,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::core,
     nullptr,
     "core",
     added,
     "RF-1.6"},
    {"--reference",
     auxiliary,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::reference,
     nullptr,
     "reference",
     changed,
     "RF-1.1"},
    {"--play-manifest",
     auxiliary,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::play_manifest,
     nullptr,
     "play_manifest",
     changed,
     "RF-1.1, RF-1.6"},
    {"--pack",
     pack,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::pack,
     nullptr,
     "pack",
     changed,
     "RF-1.3"},
    {"--output",
     destination,
     path,
     true,
     false,
     {},
     {},
     no_error,
     &CheckOptions::output,
     nullptr,
     "output",
     same,
     {}},
    {"--request-id",
     destination,
     text,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::request_id,
     nullptr,
     "request_id",
     same,
     {}},
    {"--language",
     language,
     choice,
     false,
     false,
     "es",
     language_values,
     CheckOptionErrorCode::unsupported_language,
     &CheckOptions::language,
     nullptr,
     "language",
     same,
     {}},
    {"--trust-registry",
     auxiliary,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::trust_registry,
     nullptr,
     "trust_registry",
     same,
     {}},
    {"--presentation",
     presentation,
     choice,
     false,
     false,
     "none",
     presentation_values,
     CheckOptionErrorCode::unsupported_presentation,
     &CheckOptions::presentation,
     nullptr,
     "presentation",
     same,
     {}},
    {"--pack-mode",
     pack,
     choice,
     false,
     false,
     "hd",
     pack_mode_values,
     CheckOptionErrorCode::unsupported_pack_mode,
     &CheckOptions::pack_mode,
     nullptr,
     "pack_mode",
     same,
     {}},
    {"--take",
     take,
     path,
     true,
     true,
     {},
     {},
     no_error,
     nullptr,
     &CheckOptions::takes,
     "takes",
     changed,
     "RF-1.2, RF-1.4"},
    {"--profile",
     environment,
     text,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::profile,
     nullptr,
     "profile",
     added,
     "RF-1.5, RF-1.6"},
    {"--subsystems",
     environment,
     unsigned_integer,
     false,
     false,
     {},
     {},
     CheckOptionErrorCode::invalid_unsigned_value,
     &CheckOptions::subsystems,
     nullptr,
     "subsystems",
     added,
     "RF-1.5, RF-1.6"},
    {"--mute-buses",
     environment,
     unsigned_integer,
     false,
     false,
     {},
     {},
     CheckOptionErrorCode::invalid_unsigned_value,
     &CheckOptions::mute_buses,
     nullptr,
     "mute_buses",
     added,
     "RF-1.5, RF-1.6"},
    {"--video-output",
     environment,
     text,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::video_output,
     nullptr,
     "video_output",
     added,
     "RF-1.5, RF-1.6"},
    {"--patch",
     environment,
     path,
     false,
     false,
     {},
     {},
     no_error,
     &CheckOptions::patch,
     nullptr,
     "patch",
     added,
     "RF-1.5, RF-1.6"},
    {"--shaders",
     environment,
     choice,
     false,
     false,
     {},
     shaders_values,
     CheckOptionErrorCode::unsupported_shaders,
     &CheckOptions::shaders,
     nullptr,
     "shaders",
     added,
     "RF-1.5, RF-1.6"},
    {"--core-option",
     environment,
     key_value,
     false,
     true,
     {},
     {},
     CheckOptionErrorCode::malformed_core_option,
     nullptr,
     &CheckOptions::core_options,
     "core_options",
     added,
     "RF-1.5, RF-1.6"},
}};

const std::array<FixedOptionDescriptor, 9> fixed_descriptors{{
    {"query", "--index", true, {}, {}},
    {"query", "--run-id", true, {}, {}},
    {"query", "--producer-id", true, {}, {}},
    {"query", "--sequence", true, {}, {}},
    {"query", "--language", false, "es", language_values},
    {"audit", "--run-directory", true, {}, {}},
    {"audit", "--run-id", true, {}, {}},
    {"audit", "--expected-assignments", true, {}, {}},
    {"audit", "--language", false, "es", language_values},
}};

void write_quoted(std::ostringstream &output, std::string_view value) {
    output << '"';
    for (const char character : value) {
        if (character == '"' || character == '\\')
            output << '\\';
        output << character;
    }
    output << '"';
}

void write_option(std::ostringstream &output, std::string_view command, std::string_view flag,
                  bool required, bool repeatable, std::string_view default_value,
                  std::span<const std::string_view> values) {
    output << "\n[[option]]\ncommand = ";
    write_quoted(output, command);
    output << "\nflag = ";
    write_quoted(output, flag);
    output << "\nrequired = " << (required ? "true" : "false");
    output << "\nrepeatable = " << (repeatable ? "true" : "false");
    if (!default_value.empty()) {
        output << "\ndefault = ";
        write_quoted(output, default_value);
    }
    if (!values.empty()) {
        output << "\nvalues = [";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0U)
                output << ", ";
            write_quoted(output, values[index]);
        }
        output << ']';
    }
    output << '\n';
}

} // namespace

std::span<const CheckOptionDescriptor> check_option_descriptors() noexcept { return descriptors; }

std::span<const FixedOptionDescriptor> fixed_option_descriptors() noexcept {
    return fixed_descriptors;
}

const CheckOptionDescriptor *find_check_option(std::string_view flag) noexcept {
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
                                    [flag](const auto &item) { return item.flag == flag; });
    return found == descriptors.end() ? nullptr : &*found;
}

std::string_view check_option_category_code(CheckOptionCategory category) noexcept {
    switch (category) {
    case CheckOptionCategory::rom:
        return "rom";
    case CheckOptionCategory::take:
        return "take";
    case CheckOptionCategory::pack:
        return "pack";
    case CheckOptionCategory::environment:
        return "environment";
    case CheckOptionCategory::auxiliary:
        return "auxiliary";
    case CheckOptionCategory::destination:
        return "destination";
    case CheckOptionCategory::presentation:
        return "presentation";
    case CheckOptionCategory::language:
        return "language";
    }
    return "unknown";
}

std::string_view reference_correspondence_code(ReferenceCorrespondence correspondence) noexcept {
    switch (correspondence) {
    case ReferenceCorrespondence::same:
        return "same";
    case ReferenceCorrespondence::changed:
        return "changed";
    case ReferenceCorrespondence::added:
        return "added";
    }
    return "unknown";
}

std::string format_check_option_inventory(std::string_view ref, std::string_view commit) {
    std::ostringstream output;
    output << "# Inventario de opciones de ayther_audio_qa publicado por `options --format toml`\n";
    output << "schema = 1\nsource = \"Ayther-Dev/AYTHER-Runtime\"\nref = ";
    write_quoted(output, ref);
    output << "\ncommit = ";
    write_quoted(output, commit);
    output << "\n\n[limits]\ncheck_arguments = " << max_check_arguments
           << "\ncheck_option_value_bytes = " << max_check_option_value_bytes
           << "\ncheck_takes = " << max_check_takes << "\nidentity_bytes = " << max_identity_bytes
           << '\n';
    for (const auto &item : descriptors)
        write_option(output, "check", item.flag, item.required, item.repeatable, item.default_value,
                     item.values);
    for (const auto &item : fixed_descriptors)
        write_option(output, item.command, item.flag, item.required, false, item.default_value,
                     item.values);
    return output.str();
}

std::string format_check_usage() {
    std::ostringstream output;
    output << "audio_qa_usage: ayther_audio_qa check";
    for (const auto &item : descriptors) {
        output << ' ' << (item.required ? "" : "[") << item.flag << " <";
        if (item.kind == CheckOptionKind::choice) {
            for (std::size_t index = 0; index < item.values.size(); ++index)
                output << (index == 0U ? "" : "|") << item.values[index];
        } else {
            output << (item.kind == CheckOptionKind::path               ? "path"
                       : item.kind == CheckOptionKind::unsigned_integer ? "uint32"
                       : item.kind == CheckOptionKind::key_value        ? "key=value"
                                                                        : "text");
        }
        output << '>' << (item.repeatable ? "..." : "") << (item.required ? "" : "]");
    }
    output << "\naudio_qa_usage: ayther_audio_qa options --format toml";
    output << "\naudio_qa_usage: ayther_audio_qa query|audit <options>";
    return output.str();
}

} // namespace ayther::audio_qa
