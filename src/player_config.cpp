// ---------------------------------------------------------------------------
// player_config.cpp — see player_config.h for the public contract.
// ---------------------------------------------------------------------------
#include "player_config.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <system_error>

namespace ayther {
namespace {

/// Extract the unquoted value from a `key = value` line.
std::string value_of(const std::string& line) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return {};
    std::string v = line.substr(eq + 1);
    size_t a = 0;
    while (a < v.size() && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
    v = v.substr(a);
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

bool starts(const std::string& l, const char* k) { return l.rfind(k, 0) == 0; }

}  // namespace

std::string config_key_sanitize(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '.' || c == '_' || c == '-') o += c;
        else o += '_';
    }
    // Avoid a hidden, ambiguous `.toml` file for an empty component.
    if (o.empty()) o = "sin_nombre";
    // Windows strips trailing dots, so such a path cannot be reopened under
    // the same spelling used to create it.
    while (!o.empty() && o.back() == '.') o.pop_back();
    if (o.empty()) o = "sin_nombre";
    return o;
}

std::filesystem::path player_config_path(const std::filesystem::path& dir,
                                         const std::string& game_id,
                                         const std::string& pack_name) {
    std::string key = config_key_sanitize(game_id);
    if (!pack_name.empty()) key += "__" + config_key_sanitize(pack_name);
    return dir / (key + ".toml");
}

PlayerConfig player_config_load(const std::filesystem::path& file) {
    PlayerConfig c;
    std::ifstream f(file);
    if (!f) return c;   // First run: defaults are expected, not an error.
    std::string line;
    while (std::getline(f, line)) {
        size_t s = 0;
        while (s < line.size() && std::isspace(static_cast<unsigned char>(line[s]))) ++s;
        const std::string t = line.substr(s);
        if (t.empty() || t[0] == '#') continue;

        if      (starts(t, "profile"))    c.profile = value_of(t);
        else if (starts(t, "output"))     c.output  = value_of(t);   // #296
        else if (starts(t, "subsystems")) {
            c.subsystems = static_cast<uint32_t>(std::strtoul(value_of(t).c_str(), nullptr, 10));
            c.have_subsystems = true;
        }
        else if (starts(t, "shaders"))    c.shaders_on = value_of(t) == "true";
        else if (starts(t, "hd"))         c.hd_on      = value_of(t) == "true";
        else if (starts(t, "bus_gain")) {
            // bus_gain = [1.0, 1.0, 0.5, 1.0]
            std::string v = value_of(t);
            for (char& ch : v) if (ch == '[' || ch == ']' || ch == ',') ch = ' ';
            std::istringstream is(v);
            for (int i = 0; i < 4; ++i) {
                float g = 1.0f;
                if (is >> g) c.bus_gain[i] = g;
            }
        }
        else if (starts(t, "bus_muted")) {
            std::string v = value_of(t);
            for (char& ch : v) if (ch == '[' || ch == ']' || ch == ',') ch = ' ';
            std::istringstream is(v);
            std::string w;
            for (int i = 0; i < 4 && (is >> w); ++i) c.bus_muted[i] = (w == "true");
        }
    }
    return c;
}

bool player_config_save(const std::filesystem::path& file, const PlayerConfig& c) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "# Configuración de AYTHER para esta combinación de juego y pack (#299).\n"
         "# La escribe el panel in-game; se puede editar a mano.\n\n";
    f << "profile = \"" << c.profile << "\"\n";
    // Preserve an empty value when the user made no explicit choice (#296).
    // Persisting a resolved recommendation would turn it into user policy.
    f << "output = \"" << c.output << "\"\n";
    // Always write the mask, including zero, so a later read distinguishes an
    // explicit "disable all" choice from an unset preference.
    f << "subsystems = " << c.subsystems << "\n";
    f << "shaders = " << (c.shaders_on ? "true" : "false") << "\n";
    f << "hd = " << (c.hd_on ? "true" : "false") << "\n";
    f << "bus_gain = [";
    for (int i = 0; i < 4; ++i) f << (i ? ", " : "") << c.bus_gain[i];
    f << "]\n";
    f << "bus_muted = [";
    for (int i = 0; i < 4; ++i) f << (i ? ", " : "") << (c.bus_muted[i] ? "true" : "false");
    f << "]\n";
    return static_cast<bool>(f);
}

}  // namespace ayther
