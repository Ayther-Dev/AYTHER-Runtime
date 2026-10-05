#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_check_arguments = 4096;
inline constexpr std::size_t max_check_option_value_bytes = 32768;
inline constexpr std::size_t max_check_takes = 1024;

// Spec 002 (contracts.md C5): ROM and core are explicit selections; the replay
// conditions that used to arrive only through the Play manifest or the reference
// are explicit options too. Empty strings mean "not given".
struct CheckOptions {
    std::string runtime;
    std::string rom;
    std::string core;
    std::string reference;
    std::string play_manifest;
    std::string pack;
    std::string pack_mode = "hd";
    std::string trust_registry;
    std::vector<std::string> takes;
    std::string output;
    std::string request_id;
    std::string language = "es";
    std::string presentation = "none";
    std::string profile;
    std::string subsystems;
    std::string mute_buses;
    std::string video_output;
    std::string patch;
    std::string shaders;
    std::vector<std::string> core_options;
    // RF-1.6 (D-4): the options written on the command line, so that a value equal to its
    // default still has the explicit origin.
    std::vector<std::string> given;

    [[nodiscard]] bool was_given(std::string_view flag) const noexcept {
        for (const auto &option : given)
            if (option == flag)
                return true;
        return false;
    }
};

enum class CheckOptionErrorCode {
    too_many_arguments,
    unknown_option,
    missing_value,
    empty_value,
    value_too_long,
    duplicate_option,
    too_many_takes,
    missing_required_option,
    unsupported_language,
    invalid_number,
    unsupported_presentation,
    unsupported_pack_mode,
    unsupported_shaders,
    invalid_unsigned_value,
    malformed_core_option,
    too_many_values,
};

struct CheckOptionError {
    CheckOptionErrorCode code{};
    std::string option;
    std::size_t argument_index{};
};

class CheckOptionsParseResult final {
  public:
    explicit CheckOptionsParseResult(CheckOptions options);
    explicit CheckOptionsParseResult(CheckOptionError error);

    [[nodiscard]] const CheckOptions *options() const noexcept;
    [[nodiscard]] const CheckOptionError *error() const noexcept;

  private:
    std::variant<CheckOptions, CheckOptionError> result_;
};

[[nodiscard]] CheckOptionsParseResult
parse_check_options(std::span<const std::string_view> arguments);
[[nodiscard]] std::string_view check_option_error_code(CheckOptionErrorCode code) noexcept;

} // namespace ayther::audio_qa
