// ---------------------------------------------------------------------------
// player_config.cpp — see player_config.h for the public contract.
// ---------------------------------------------------------------------------
#include "player_config.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ayther {
namespace {

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool parse_unsigned(const std::string_view text,
                                  std::uint32_t& output) noexcept {
    const auto value = trim(text);
    if (value.empty() || value.front() == '+' || value.front() == '-') {
        return false;
    }
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] bool parse_float(const std::string_view text,
                               float& output) noexcept {
    const auto value = trim(text);
    if (value.empty()) {
        return false;
    }
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end &&
           std::isfinite(output) && output >= 0.0F && output <= 1.0F;
}

[[nodiscard]] bool parse_bool(const std::string_view text,
                              bool& output) noexcept {
    const auto value = trim(text);
    if (value == "true") {
        output = true;
        return true;
    }
    if (value == "false") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_string(const std::string_view text,
                                std::string& output) {
    const auto value = trim(text);
    if (value.size() < 2U || value.front() != '"' || value.back() != '"') {
        return false;
    }

    std::string parsed;
    parsed.reserve(value.size() - 2U);
    for (std::size_t index = 1U; index + 1U < value.size(); ++index) {
        const char current = value[index];
        if (current != '\\') {
            if (static_cast<unsigned char>(current) < 0x20U) {
                return false;
            }
            parsed.push_back(current);
            continue;
        }
        ++index;
        if (index + 1U >= value.size()) {
            return false;
        }
        switch (value[index]) {
        case '"': parsed.push_back('"'); break;
        case '\\': parsed.push_back('\\'); break;
        case 'n': parsed.push_back('\n'); break;
        case 'r': parsed.push_back('\r'); break;
        case 't': parsed.push_back('\t'); break;
        default: return false;
        }
    }
    output = std::move(parsed);
    return true;
}

[[nodiscard]] std::string escape_string(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8U);
    for (const char current : value) {
        switch (current) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(current); break;
        }
    }
    return escaped;
}

[[nodiscard]] bool split_array(const std::string_view text,
                               std::vector<std::string_view>& elements) {
    const auto value = trim(text);
    if (value.size() < 2U || value.front() != '[' || value.back() != ']') {
        return false;
    }
    std::string_view body = value.substr(1U, value.size() - 2U);
    elements.clear();
    while (true) {
        const auto comma = body.find(',');
        const auto element = trim(body.substr(0U, comma));
        if (element.empty()) {
            return false;
        }
        elements.push_back(element);
        if (comma == std::string_view::npos) {
            break;
        }
        body.remove_prefix(comma + 1U);
    }
    return elements.size() == player_config_audio_bus_count;
}

[[nodiscard]] PlayerConfigLoadResult invalid_result(
    const PlayerConfigLoadStatus status, const std::size_t line,
    std::string diagnostic) {
    return PlayerConfigLoadResult{
        PlayerConfig{}, status, line, std::move(diagnostic)};
}

}  // namespace

std::string config_key_sanitize(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const char current : value) {
        const auto code_unit = static_cast<unsigned char>(current);
        if ((code_unit >= 'a' && code_unit <= 'z') ||
            (code_unit >= 'A' && code_unit <= 'Z') ||
            (code_unit >= '0' && code_unit <= '9') || current == '.' ||
            current == '_' || current == '-') {
            output += current;
        } else {
            output += '_';
        }
    }
    if (output.empty()) {
        output = "sin_nombre";
    }
    while (!output.empty() && output.back() == '.') {
        output.pop_back();
    }
    return output.empty() ? std::string{"sin_nombre"} : output;
}

std::filesystem::path player_config_path(const std::filesystem::path& directory,
                                         const std::string& game_id,
                                         const std::string& pack_name) {
    std::string key = config_key_sanitize(game_id);
    if (!pack_name.empty()) {
        key += "__" + config_key_sanitize(pack_name);
    }
    return directory / (key + ".toml");
}

PlayerConfigLoadResult
player_config_load_checked(const std::filesystem::path& file) {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(file, filesystem_error)) {
        return filesystem_error
                   ? invalid_result(PlayerConfigLoadStatus::io_error, 0U,
                                    "no se pudo consultar el archivo")
                   : PlayerConfigLoadResult{
                         PlayerConfig{}, PlayerConfigLoadStatus::missing, 0U, {}};
    }

    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return invalid_result(PlayerConfigLoadStatus::io_error, 0U,
                              "no se pudo abrir el archivo");
    }

    PlayerConfig candidate;
    std::uint32_t version = 0U;  // Version 0 is the legacy field set.
    std::unordered_set<std::string> seen_keys;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        const auto text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) {
            return invalid_result(PlayerConfigLoadStatus::invalid, line_number,
                                  "se esperaba clave = valor");
        }
        const auto key_view = trim(text.substr(0U, equals));
        const auto value = trim(text.substr(equals + 1U));
        if (key_view.empty() || value.empty()) {
            return invalid_result(PlayerConfigLoadStatus::invalid, line_number,
                                  "la clave o el valor estan vacios");
        }
        const std::string key{key_view};
        if (!seen_keys.insert(key).second) {
            return invalid_result(PlayerConfigLoadStatus::invalid, line_number,
                                  "la clave esta duplicada: " + key);
        }

        bool valid = true;
        if (key == "format_version") {
            valid = parse_unsigned(value, version);
        } else if (key == "profile") {
            valid = parse_string(value, candidate.profile);
        } else if (key == "output") {
            valid = parse_string(value, candidate.output);
        } else if (key == "subsystems") {
            valid = parse_unsigned(value, candidate.subsystems);
            candidate.have_subsystems = valid;
        } else if (key == "shaders") {
            valid = parse_bool(value, candidate.shaders_on);
        } else if (key == "hd") {
            valid = parse_bool(value, candidate.hd_on);
        } else if (key == "bus_gain") {
            std::vector<std::string_view> elements;
            valid = split_array(value, elements);
            for (std::size_t index = 0U; valid && index < elements.size(); ++index) {
                valid = parse_float(elements[index], candidate.bus_gain[index]);
            }
        } else if (key == "bus_muted") {
            std::vector<std::string_view> elements;
            valid = split_array(value, elements);
            for (std::size_t index = 0U; valid && index < elements.size(); ++index) {
                valid = parse_bool(elements[index], candidate.bus_muted[index]);
            }
        }

        if (!valid) {
            return invalid_result(PlayerConfigLoadStatus::invalid, line_number,
                                  "valor invalido para " + key);
        }
    }
    if (input.bad()) {
        return invalid_result(PlayerConfigLoadStatus::io_error, line_number,
                              "fallo al leer el archivo completo");
    }
    if (version > player_config_format_version) {
        return invalid_result(PlayerConfigLoadStatus::unsupported_version,
                              line_number,
                              "version de configuracion no soportada");
    }
    return PlayerConfigLoadResult{
        std::move(candidate), PlayerConfigLoadStatus::loaded, 0U, {}};
}

PlayerConfig player_config_load(const std::filesystem::path& file) {
    return player_config_load_checked(file).config;
}

bool player_config_save(const std::filesystem::path& file,
                        const PlayerConfig& config) {
    for (const float gain : config.bus_gain) {
        if (!std::isfinite(gain) || gain < 0.0F || gain > 1.0F) {
            return false;
        }
    }

    std::error_code filesystem_error;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(), filesystem_error);
        if (filesystem_error) {
            return false;
        }
    }

    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::filesystem::path backup = file;
    backup += ".bak";
    std::filesystem::remove(temporary, filesystem_error);
    filesystem_error.clear();

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "# Configuracion de AYTHER para esta combinacion de juego y pack.\n"
              "# La escribe el panel in-game; se puede editar a mano.\n\n";
    output << "format_version = " << player_config_format_version << '\n';
    output << "profile = \"" << escape_string(config.profile) << "\"\n";
    output << "output = \"" << escape_string(config.output) << "\"\n";
    output << "subsystems = " << config.subsystems << '\n';
    output << "shaders = " << (config.shaders_on ? "true" : "false") << '\n';
    output << "hd = " << (config.hd_on ? "true" : "false") << '\n';
    output << "bus_gain = [" << std::setprecision(9);
    for (std::size_t index = 0U; index < config.bus_gain.size(); ++index) {
        output << (index == 0U ? "" : ", ") << config.bus_gain[index];
    }
    output << "]\n";
    output << "bus_muted = [";
    for (std::size_t index = 0U; index < config.bus_muted.size(); ++index) {
        output << (index == 0U ? "" : ", ")
               << (config.bus_muted[index] ? "true" : "false");
    }
    output << "]\n";
    output.flush();
    const bool write_succeeded = static_cast<bool>(output);
    output.close();
    if (!write_succeeded || output.fail()) {
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }

    const bool had_previous = std::filesystem::exists(file, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    if (had_previous) {
        std::filesystem::remove(backup, filesystem_error);
        filesystem_error.clear();
        std::filesystem::rename(file, backup, filesystem_error);
        if (filesystem_error) {
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }

    std::filesystem::rename(temporary, file, filesystem_error);
    if (filesystem_error) {
        if (had_previous) {
            std::error_code restore_error;
            std::filesystem::rename(backup, file, restore_error);
        }
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    if (had_previous) {
        std::filesystem::remove(backup, filesystem_error);
    }
    return true;
}

}  // namespace ayther
