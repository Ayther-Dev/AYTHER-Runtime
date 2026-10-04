#include "runtime_options.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

void check(const bool condition, const char *description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

ayther::runtime::RuntimeOptionsParseResult
parse(const std::initializer_list<std::string_view> arguments) {
    std::vector<std::string> storage;
    storage.reserve(arguments.size());
    for (const std::string_view argument : arguments) {
        storage.emplace_back(argument);
    }

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (std::string &argument : storage) {
        argv.push_back(argument.data());
    }

    return ayther::runtime::RuntimeOptions::parse(static_cast<int>(argv.size()), argv.data());
}

void expect_error(const std::initializer_list<std::string_view> arguments,
                  const ayther::runtime::RuntimeOptionErrorCode expected_code,
                  const std::string_view expected_option, const char *description) {
    const auto parsed = parse(arguments);
    const auto *error = parsed.error();
    check(error != nullptr, description);
    if (error == nullptr) {
        return;
    }
    check(error->code == expected_code, "error has the expected typed code");
    check(error->option == expected_option, "error identifies the CLI option");
    check(!ayther::runtime::describe(*error).empty(), "error has a human-readable diagnostic");
}

} // namespace

int main() {
    using ayther::runtime::RuntimeOptionErrorCode;

    std::printf("== runtime_options_test (MAD-006) ==\n");

    const auto revisions =
        parse({"ayther_runtime", "--rom-revision", "rom-token", "--pack-revision", "pack-token"});
    check(revisions.options() && revisions.options()->rom_revision == "rom-token" &&
              revisions.options()->pack_revision == "pack-token",
          "reservation tokens are preserved independently");
    expect_error({"ayther_runtime", "--rom-revision"}, RuntimeOptionErrorCode::missing_value,
                 "--rom-revision", "a requested ROM reservation requires a token");
    expect_error({"ayther_runtime", "--pack-revision", ""}, RuntimeOptionErrorCode::empty_value,
                 "--pack-revision", "an empty pack token cannot silently disable reservation");

    {
        const auto parsed = parse({
            "ayther_runtime",
            "--core",
            "core.dll",
            "--rom",
            "game.rom",
            "--subsystems",
            "4294967295",
            "--mute-buses",
            "17",
            "--frames",
            "0",
            "--capture-at",
            "1,42,18446744073709551615",
            "--play-protocol-version",
            "1",
            "--input-map",
            "controls.toml",
        });
        const auto *options = parsed.options();
        check(options != nullptr, "all numeric options accept valid values");
        if (options != nullptr) {
            check(options->subsystems == UINT32_MAX,
                  "--subsystems accepts the complete uint32_t domain");
            check(options->mute_buses == 17u, "--mute-buses parses a uint32_t mask");
            check(options->frames_limit == 0u,
                  "--frames 0 preserves the unlimited-session meaning");
            check(options->capture_at == std::vector<std::uint64_t>{1u, 42u, UINT64_MAX},
                  "--capture-at parses a non-empty positive list");
            check(options->play_protocol_version == 1u,
                  "Play can negotiate the status protocol before startup");
            check(options->input_map_path == "controls.toml",
                  "Play can pass the per-session input map");
        }
    }

    {
        const auto parsed = parse({
            "ayther_runtime",
            "positional-core",
            "positional-rom",
            "--subsystems",
            "0",
            "--mute-buses",
            "0",
            "--frames",
            "25",
            "--capture-at",
            "7",
            "--shaders",
            "--core-option",
            "region=ntsc",
        });
        const auto *options = parsed.options();
        check(options != nullptr, "valid mixed options parse successfully");
        if (options != nullptr) {
            check(options->core_path == "positional-core" && options->rom_path == "positional-rom",
                  "legacy positional core and ROM remain supported");
            check(options->subsystems == 0u && options->mute_buses == 0u,
                  "explicit zero masks remain distinguishable from absence");
            check(options->frames_limit == 25u, "--frames accepts a positive bounded run");
            check(options->capture_at == std::vector<std::uint64_t>{7u},
                  "--capture-at accepts one positive frame");
            check(options->shaders == true, "non-numeric options remain part of RuntimeOptions");
            check(options->core_options.size() == 1u &&
                      options->core_options.front().first == "region" &&
                      options->core_options.front().second == "ntsc",
                  "repeatable core options remain structured");
        }
    }

    expect_error({"ayther_runtime", "--subsystems"}, RuntimeOptionErrorCode::missing_value,
                 "--subsystems", "--subsystems rejects a missing value");
    expect_error({"ayther_runtime", "--subsystems", "-1"}, RuntimeOptionErrorCode::invalid_sign,
                 "--subsystems", "--subsystems rejects negatives");
    expect_error({"ayther_runtime", "--subsystems", "4294967296"},
                 RuntimeOptionErrorCode::integer_overflow, "--subsystems",
                 "--subsystems rejects uint32_t overflow");
    expect_error({"ayther_runtime", "--subsystems", "12mask"},
                 RuntimeOptionErrorCode::trailing_characters, "--subsystems",
                 "--subsystems rejects trailing text");

    expect_error({"ayther_runtime", "--mute-buses", ""}, RuntimeOptionErrorCode::empty_value,
                 "--mute-buses", "--mute-buses rejects an empty value");
    expect_error({"ayther_runtime", "--mute-buses", "+1"}, RuntimeOptionErrorCode::invalid_sign,
                 "--mute-buses", "--mute-buses rejects an explicit plus sign");
    expect_error({"ayther_runtime", "--mute-buses", "none"},
                 RuntimeOptionErrorCode::invalid_integer, "--mute-buses",
                 "--mute-buses rejects non-numeric text");
    expect_error({"ayther_runtime", "--mute-buses", "4294967296"},
                 RuntimeOptionErrorCode::integer_overflow, "--mute-buses",
                 "--mute-buses rejects uint32_t overflow");

    expect_error({"ayther_runtime", "--frames"}, RuntimeOptionErrorCode::missing_value, "--frames",
                 "--frames rejects a missing value");
    expect_error({"ayther_runtime", "--frames", "--capture-at", "1"},
                 RuntimeOptionErrorCode::missing_value, "--frames",
                 "--frames detects a following option instead of a value");
    expect_error({"ayther_runtime", "--frames", "-1"}, RuntimeOptionErrorCode::invalid_sign,
                 "--frames", "--frames rejects negatives");
    expect_error({"ayther_runtime", "--frames", "18446744073709551616"},
                 RuntimeOptionErrorCode::integer_overflow, "--frames",
                 "--frames rejects uint64_t overflow");
    expect_error({"ayther_runtime", "--frames", "12foo"},
                 RuntimeOptionErrorCode::trailing_characters, "--frames",
                 "--frames rejects trailing text");

    expect_error({"ayther_runtime", "--capture-at"}, RuntimeOptionErrorCode::missing_value,
                 "--capture-at", "--capture-at rejects a missing list");
    expect_error({"ayther_runtime", "--capture-at", ""}, RuntimeOptionErrorCode::empty_value,
                 "--capture-at", "--capture-at rejects an empty list");
    expect_error({"ayther_runtime", "--capture-at", "0"},
                 RuntimeOptionErrorCode::value_out_of_domain, "--capture-at",
                 "--capture-at requires strictly positive frames");
    expect_error({"ayther_runtime", "--capture-at", "1,-2"}, RuntimeOptionErrorCode::invalid_sign,
                 "--capture-at", "--capture-at rejects negative elements");
    expect_error({"ayther_runtime", "--capture-at", "18446744073709551616"},
                 RuntimeOptionErrorCode::integer_overflow, "--capture-at",
                 "--capture-at rejects uint64_t overflow");
    expect_error({"ayther_runtime", "--capture-at", "1,2x"},
                 RuntimeOptionErrorCode::trailing_characters, "--capture-at",
                 "--capture-at rejects element trailing text");
    expect_error({"ayther_runtime", "--capture-at", ",1"}, RuntimeOptionErrorCode::malformed_list,
                 "--capture-at", "--capture-at rejects a leading comma");
    expect_error({"ayther_runtime", "--capture-at", "1,"}, RuntimeOptionErrorCode::malformed_list,
                 "--capture-at", "--capture-at rejects a trailing comma");
    expect_error({"ayther_runtime", "--capture-at", "1,,2"}, RuntimeOptionErrorCode::malformed_list,
                 "--capture-at", "--capture-at rejects repeated commas");

    expect_error({"ayther_runtime", "--input-map"}, RuntimeOptionErrorCode::missing_value,
                 "--input-map", "--input-map rejects a missing path");
    expect_error({"ayther_runtime", "--input-map", ""}, RuntimeOptionErrorCode::empty_value,
                 "--input-map", "--input-map rejects an empty path");

    check(ayther::runtime::runtime_cli_error_exit_code == 64,
          "malformed CLI input has the stable documented exit code 64");
    expect_error({"ayther_runtime", "--trust-registry"}, RuntimeOptionErrorCode::missing_value,
                 "--trust-registry", "registry needs a path");
    expect_error({"ayther_runtime", "--trust-registry", ""}, RuntimeOptionErrorCode::empty_value,
                 "--trust-registry", "registry path cannot be empty");
    const auto trusted = parse({"ayther_runtime", "--trust-registry", "trust files/keys.toml"});
    check(trusted.options() && trusted.options()->trust_registry_path == "trust files/keys.toml",
          "registry path with spaces is preserved");

    const auto ordinary =
        parse({"ayther_runtime", "--core", "core.dll", "--rom", "game.rom", "--pack", "pack.ay",
               "--manifest", "launch.toml", "--saves-dir", "saves", "--rom-crc32", "1234abcd"});
    check(ordinary.options() && !ordinary.options()->qa_session &&
              !ordinary.options()->qa_capabilities &&
              ordinary.options()->qa_control_channel.empty() &&
              ordinary.options()->qa_data_channel.empty() && ordinary.options()->qa_run_id.empty(),
          "ordinary Play arguments keep the existing Runtime path without QA mode");

    const auto qa_session =
        parse({"ayther_runtime", "--qa-session", "--qa-control-channel", "101", "--qa-data-channel",
               "202", "--qa-run-id", "run-qa170", "--core", "core.dll", "--rom", "game.rom"});
    check(qa_session.options() && qa_session.options()->qa_session &&
              qa_session.options()->qa_control_channel == "101" &&
              qa_session.options()->qa_data_channel == "202" &&
              qa_session.options()->qa_run_id == "run-qa170",
          "QA session options remain typed and separate from ordinary launch");
    const auto qa_capabilities = parse({"ayther_runtime", "--qa-capabilities"});
    const auto visible = parse({"ayther_runtime", "--qa-session", "--qa-presentation", "visible"});
    check(visible.options() && visible.options()->qa_presentation == "visible",
          "QA visible presentation is parsed explicitly");
    expect_error({"ayther_runtime", "--qa-presentation", "invalid"},
                 RuntimeOptionErrorCode::value_out_of_domain, "--qa-presentation",
                 "invalid QA presentation is rejected");
    check(qa_capabilities.options() && qa_capabilities.options()->qa_capabilities,
          "QA capability query is an explicit mode");
    // Spec 002, BR-135 (contracts.md C1-3, C1-6): the supervisor tells the Runtime the
    // position of its take in the request and whether it is the last one.
    const auto take =
        parse({"ayther_runtime", "--qa-session", "--qa-take-position", "3", "--qa-last-take"});
    check(take.options() && take.options()->qa_take_position == 3U && take.options()->qa_last_take,
          "RF-2.3: the take position and the last take are parsed");
    const auto single = parse({"ayther_runtime", "--qa-session"});
    check(single.options() && single.options()->qa_take_position == 0U &&
              !single.options()->qa_last_take,
          "without them the take is the first and not the last");
    expect_error({"ayther_runtime", "--qa-take-position", "-1"},
                 RuntimeOptionErrorCode::invalid_sign, "--qa-take-position",
                 "a negative take position is rejected");
    // Spec 002 (contracts.md C5): the core probe can also load a ROM.
    const auto rom_probe = parse({"ayther_runtime", "--probe-core", "core.dll", "--probe-rom",
                                  "game.md", "--core-option", "region=pal"});
    check(rom_probe.options() && rom_probe.options()->probe_core == "core.dll" &&
              rom_probe.options()->probe_rom == "game.md" &&
              rom_probe.options()->core_options.size() == 1U,
          "the ROM probe keeps the ROM and the core options of the launch");
    expect_error({"ayther_runtime", "--probe-core", "core.dll", "--probe-rom"},
                 RuntimeOptionErrorCode::missing_value, "--probe-rom", "the ROM probe needs a ROM");
    expect_error({"ayther_runtime", "--qa-control-channel"}, RuntimeOptionErrorCode::missing_value,
                 "--qa-control-channel", "QA control channel needs a token");

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
