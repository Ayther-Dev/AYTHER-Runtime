#include "runtime_options.h"

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace ayther::runtime {

namespace {

using ValueResult = std::variant<std::string_view, RuntimeOptionError>;

[[nodiscard]] RuntimeOptionError make_error(const RuntimeOptionErrorCode code,
                                            const std::string_view option,
                                            const std::string_view value,
                                            const std::size_t argument_index) {
    return RuntimeOptionError{code, std::string{option}, std::string{value},
                              argument_index};
}

template <typename UInt> using UnsignedResult = std::variant<UInt, RuntimeOptionError>;

template <typename UInt>
[[nodiscard]] UnsignedResult<UInt>
parse_unsigned(const std::string_view text, const std::string_view option,
               const std::size_t argument_index, const bool require_positive = false) {
    static_assert(std::is_unsigned_v<UInt>);

    if (text.empty()) {
        return make_error(RuntimeOptionErrorCode::empty_value, option, text,
                          argument_index);
    }
    if (text.front() == '+' || text.front() == '-') {
        return make_error(RuntimeOptionErrorCode::invalid_sign, option, text,
                          argument_index);
    }

    UInt value{};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec == std::errc::invalid_argument) {
        return make_error(RuntimeOptionErrorCode::invalid_integer, option, text,
                          argument_index);
    }
    if (parsed.ec == std::errc::result_out_of_range) {
        return make_error(RuntimeOptionErrorCode::integer_overflow, option, text,
                          argument_index);
    }
    if (parsed.ptr != end) {
        return make_error(RuntimeOptionErrorCode::trailing_characters, option, text,
                          argument_index);
    }
    if (require_positive && value == 0) {
        return make_error(RuntimeOptionErrorCode::value_out_of_domain, option, text,
                          argument_index);
    }
    return value;
}

using CaptureListResult = std::variant<std::vector<std::uint64_t>, RuntimeOptionError>;

[[nodiscard]] CaptureListResult parse_capture_list(const std::string_view text,
                                                   const std::size_t argument_index) {
    constexpr std::string_view option = "--capture-at";
    if (text.empty()) {
        return make_error(RuntimeOptionErrorCode::empty_value, option, text,
                          argument_index);
    }
    if (text.front() == ',' || text.back() == ',' ||
        text.find(",,") != std::string_view::npos) {
        return make_error(RuntimeOptionErrorCode::malformed_list, option, text,
                          argument_index);
    }

    std::vector<std::uint64_t> frames;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = text.substr(begin, end - begin);
        auto parsed =
            parse_unsigned<std::uint64_t>(element, option, argument_index, true);
        if (const auto* error = std::get_if<RuntimeOptionError>(&parsed)) {
            return *error;
        }
        frames.push_back(std::get<std::uint64_t>(parsed));
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    return frames;
}

[[nodiscard]] const char* error_reason(const RuntimeOptionErrorCode code) {
    switch (code) {
    case RuntimeOptionErrorCode::missing_value:
        return "falta un valor";
    case RuntimeOptionErrorCode::empty_value:
        return "el valor esta vacio";
    case RuntimeOptionErrorCode::invalid_sign:
        return "el signo no es valido";
    case RuntimeOptionErrorCode::invalid_integer:
        return "el valor no es un entero decimal";
    case RuntimeOptionErrorCode::integer_overflow:
        return "el entero excede el rango admitido";
    case RuntimeOptionErrorCode::trailing_characters:
        return "hay texto residual despues del entero";
    case RuntimeOptionErrorCode::value_out_of_domain:
        return "el valor esta fuera del dominio admitido";
    case RuntimeOptionErrorCode::malformed_list:
        return "la lista esta malformada";
    case RuntimeOptionErrorCode::malformed_core_option:
        return "se esperaba clave=valor con una clave no vacia";
    }
    return "el valor no es valido";
}

}  // namespace

RuntimeOptionsParseResult::RuntimeOptionsParseResult(RuntimeOptions options)
    : result_(std::move(options)) {}

RuntimeOptionsParseResult::RuntimeOptionsParseResult(RuntimeOptionError error)
    : result_(std::move(error)) {}

RuntimeOptions* RuntimeOptionsParseResult::options() noexcept {
    return std::get_if<RuntimeOptions>(&result_);
}

const RuntimeOptions* RuntimeOptionsParseResult::options() const noexcept {
    return std::get_if<RuntimeOptions>(&result_);
}

const RuntimeOptionError* RuntimeOptionsParseResult::error() const noexcept {
    return std::get_if<RuntimeOptionError>(&result_);
}

RuntimeOptionsParseResult RuntimeOptions::parse(const int argc, char* const argv[]) {
    RuntimeOptions options;
    std::vector<std::string> positional;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument =
            argv[index] != nullptr ? std::string_view{argv[index]} : std::string_view{};

        const auto next_value = [&](const std::string_view option) -> ValueResult {
            if (index + 1 >= argc) {
                return make_error(RuntimeOptionErrorCode::missing_value, option, {},
                                  static_cast<std::size_t>(index));
            }
            const std::string_view candidate = argv[index + 1] != nullptr
                                                   ? std::string_view{argv[index + 1]}
                                                   : std::string_view{};
            if (candidate.starts_with("--")) {
                return make_error(RuntimeOptionErrorCode::missing_value, option, {},
                                  static_cast<std::size_t>(index));
            }
            ++index;
            return candidate;
        };

        const auto read_string =
            [&](const std::string_view option,
                std::string& destination) -> std::optional<RuntimeOptionError> {
            auto value = next_value(option);
            if (const auto* error = std::get_if<RuntimeOptionError>(&value)) {
                return *error;
            }
            destination = std::get<std::string_view>(value);
            return std::nullopt;
        };

        if (argument == "--core") {
            if (auto error = read_string(argument, options.core_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--rom") {
            if (auto error = read_string(argument, options.rom_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--pack") {
            if (auto error = read_string(argument, options.pack_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--patch") {
            if (auto error = read_string(argument, options.patch_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--profile") {
            if (auto error = read_string(argument, options.profile)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--saves-dir") {
            if (auto error = read_string(argument, options.saves_directory)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--load-state") {
            if (auto error = read_string(argument, options.load_state)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--input-map") {
            if (auto error = read_string(argument, options.input_map_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
            if (options.input_map_path.empty()) {
                return RuntimeOptionsParseResult{make_error(
                    RuntimeOptionErrorCode::empty_value, argument, {},
                    static_cast<std::size_t>(index))};
            }
        } else if (argument == "--rom-crc32") {
            if (auto error = read_string(argument, options.rom_crc32)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--output") {
            if (auto error = read_string(argument, options.output)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--manifest") {
            if (auto error = read_string(argument, options.manifest_path)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--probe-core") {
            if (auto error = read_string(argument, options.probe_core)) {
                return RuntimeOptionsParseResult{std::move(*error)};
            }
        } else if (argument == "--subsystems" || argument == "--mute-buses" ||
                   argument == "--play-protocol-version") {
            auto value = next_value(argument);
            if (const auto* error = std::get_if<RuntimeOptionError>(&value)) {
                return RuntimeOptionsParseResult{*error};
            }
            auto parsed = parse_unsigned<std::uint32_t>(
                std::get<std::string_view>(value), argument,
                static_cast<std::size_t>(index));
            if (const auto* error = std::get_if<RuntimeOptionError>(&parsed)) {
                return RuntimeOptionsParseResult{*error};
            }
            if (argument == "--subsystems") {
                options.subsystems = std::get<std::uint32_t>(parsed);
            } else if (argument == "--mute-buses") {
                options.mute_buses = std::get<std::uint32_t>(parsed);
            } else {
                options.play_protocol_version =
                    std::get<std::uint32_t>(parsed);
            }
        } else if (argument == "--frames") {
            auto value = next_value(argument);
            if (const auto* error = std::get_if<RuntimeOptionError>(&value)) {
                return RuntimeOptionsParseResult{*error};
            }
            auto parsed = parse_unsigned<std::uint64_t>(
                std::get<std::string_view>(value), argument,
                static_cast<std::size_t>(index));
            if (const auto* error = std::get_if<RuntimeOptionError>(&parsed)) {
                return RuntimeOptionsParseResult{*error};
            }
            options.frames_limit = std::get<std::uint64_t>(parsed);
        } else if (argument == "--capture-at") {
            auto value = next_value(argument);
            if (const auto* error = std::get_if<RuntimeOptionError>(&value)) {
                return RuntimeOptionsParseResult{*error};
            }
            auto parsed = parse_capture_list(std::get<std::string_view>(value),
                                             static_cast<std::size_t>(index));
            if (const auto* error = std::get_if<RuntimeOptionError>(&parsed)) {
                return RuntimeOptionsParseResult{*error};
            }
            auto& frames = std::get<std::vector<std::uint64_t>>(parsed);
            options.capture_at.insert(options.capture_at.end(), frames.begin(),
                                      frames.end());
        } else if (argument == "--core-option") {
            auto value = next_value(argument);
            if (const auto* error = std::get_if<RuntimeOptionError>(&value)) {
                return RuntimeOptionsParseResult{*error};
            }
            const std::string_view core_option = std::get<std::string_view>(value);
            const std::size_t equals = core_option.find('=');
            if (equals == std::string_view::npos || equals == 0) {
                return RuntimeOptionsParseResult{
                    make_error(RuntimeOptionErrorCode::malformed_core_option, argument,
                               core_option, static_cast<std::size_t>(index))};
            }
            options.core_options.emplace_back(core_option.substr(0, equals),
                                              core_option.substr(equals + 1));
        } else if (argument == "--no-shaders") {
            options.shaders = false;
        } else if (argument == "--shaders") {
            options.shaders = true;
        } else if (argument == "--crash-test") {
            options.crash_test = true;
        } else if (argument == "--hd-compose") {
            options.hd_compose = true;
        } else {
            positional.emplace_back(argument);
        }
    }

    if (options.core_path.empty() && !positional.empty()) {
        options.core_path = positional[0];
    }
    if (options.rom_path.empty() && positional.size() >= 2) {
        options.rom_path = positional[1];
    }
    return RuntimeOptionsParseResult{std::move(options)};
}

std::string describe(const RuntimeOptionError& error) {
    std::string message = error.option;
    message += ": ";
    message += error_reason(error.code);
    if (!error.value.empty()) {
        message += " ('";
        message += error.value;
        message += "')";
    }
    return message;
}

}  // namespace ayther::runtime
