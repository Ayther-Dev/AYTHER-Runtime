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

inline constexpr int runtime_cli_error_exit_code = exit_code(RuntimeExitCode::cli_usage);

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
    std::string rom_revision;
    std::string pack_revision;
    std::string trust_registry_path;
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
    // Spec 002 (contracts.md C5): with --probe-core, also load this ROM without running it.
    std::string probe_rom;
    // Spec 002 (contracts.md C5): QA build only, probe a pack without starting a game.
    std::string probe_pack;
    std::string manifest_path;
    bool hd_compose{};
    std::optional<std::uint32_t> play_protocol_version;
    bool qa_capabilities{};
    bool qa_session{};
    std::string qa_control_channel;
    std::string qa_data_channel;
    std::string qa_run_id;
    std::string qa_presentation = "none";
    // Spec 002 (contracts.md C1-3, C1-6): the position of the take in the request and whether
    // it is the last one, for the live state and the natural end (RF-2.8).
    std::uint32_t qa_take_position{};
    bool qa_last_take{};

    [[nodiscard]] static RuntimeOptionsParseResult parse(int argc, char *const argv[]);
};

class RuntimeOptionsParseResult final {
  public:
    explicit RuntimeOptionsParseResult(RuntimeOptions options);
    explicit RuntimeOptionsParseResult(RuntimeOptionError error);

    [[nodiscard]] RuntimeOptions *options() noexcept;
    [[nodiscard]] const RuntimeOptions *options() const noexcept;
    [[nodiscard]] const RuntimeOptionError *error() const noexcept;

  private:
    std::variant<RuntimeOptions, RuntimeOptionError> result_;
};

[[nodiscard]] std::string describe(const RuntimeOptionError &error);

} // namespace ayther::runtime
