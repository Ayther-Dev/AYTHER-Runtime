#pragma once

#include "runtime_error.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::runtime {

inline constexpr int runtime_cli_error_exit_code =
    exit_code(RuntimeExitCode::cli_usage);

enum class RuntimeOptionErrorCode {
    missing_value,
    empty_value,
    invalid_sign,
    invalid_integer,
    integer_overflow,
    trailing_characters,
    value_out_of_domain,
    malformed_list,
    malformed_core_option,
};

struct RuntimeOptionError {
    RuntimeOptionErrorCode code{};
    std::string option;
    std::string value;
    std::size_t argument_index{};
};

class RuntimeOptionsParseResult;

struct RuntimeOptions {
    std::string core_path;
    std::string rom_path;
    std::string pack_path;
    std::string patch_path;
    std::string profile;
    std::string saves_directory;
    std::string rom_crc32;
    std::string load_state;
    std::string input_map_path;
    std::vector<std::pair<std::string, std::string>> core_options;
    std::optional<std::uint32_t> subsystems;
    std::optional<std::uint32_t> mute_buses;
    std::optional<bool> shaders;
    std::string output;
    std::uint64_t frames_limit{};
    std::vector<std::uint64_t> capture_at;
    bool crash_test{};
    std::string probe_core;
    std::string manifest_path;
    bool hd_compose{};
    std::optional<std::uint32_t> play_protocol_version;

    [[nodiscard]] static RuntimeOptionsParseResult parse(int argc, char* const argv[]);
};

class RuntimeOptionsParseResult final {
public:
    explicit RuntimeOptionsParseResult(RuntimeOptions options);
    explicit RuntimeOptionsParseResult(RuntimeOptionError error);

    [[nodiscard]] RuntimeOptions* options() noexcept;
    [[nodiscard]] const RuntimeOptions* options() const noexcept;
    [[nodiscard]] const RuntimeOptionError* error() const noexcept;

private:
    std::variant<RuntimeOptions, RuntimeOptionError> result_;
};

[[nodiscard]] std::string describe(const RuntimeOptionError& error);

}  // namespace ayther::runtime
