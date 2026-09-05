#include "input_map.h"

#include <SDL3/SDL_keyboard.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace ayther {

class InputMapBuilder final {
public:
    [[nodiscard]] static KeyboardBinding*
    find_keyboard(InputMap& map, const engine::JoypadButton action) noexcept {
        const auto found = std::ranges::find_if(
            map.keyboard_, [action](const auto& binding) {
                return binding.action == action;
            });
        return found == map.keyboard_.end() ? nullptr : &*found;
    }

    [[nodiscard]] static GamepadBinding*
    find_gamepad(InputMap& map, const engine::JoypadButton action) noexcept {
        const auto found = std::ranges::find_if(
            map.gamepad_, [action](const auto& binding) {
                return binding.action == action;
            });
        return found == map.gamepad_.end() ? nullptr : &*found;
    }
};

namespace {

using engine::JoypadButton;

[[nodiscard]] std::optional<JoypadButton>
action_from_key(const std::string_view key) noexcept {
    constexpr std::array actions{
        JoypadButton::up, JoypadButton::down, JoypadButton::left,
        JoypadButton::right, JoypadButton::b, JoypadButton::a,
        JoypadButton::y, JoypadButton::x, JoypadButton::l,
        JoypadButton::r, JoypadButton::start, JoypadButton::select,
    };
    const auto found = std::ranges::find_if(actions, [key](const auto action) {
        return input_action_key(action) == key;
    });
    if (found == actions.end()) {
        return std::nullopt;
    }
    return *found;
}

[[nodiscard]] InputMapError make_error(
    const InputMapErrorCode code, const std::filesystem::path& path,
    const std::string_view section = {}, const std::string_view key = {},
    const std::string_view value = {}, const std::string_view detail = {}) {
    return InputMapError{code,
                         path,
                         std::string{section},
                         std::string{key},
                         std::string{value},
                         std::string{detail}};
}

[[nodiscard]] std::optional<SDL_GamepadButton>
gamepad_button_from_contract(const std::string_view name) noexcept {
    if (name == "south") {
        return SDL_GAMEPAD_BUTTON_SOUTH;
    }
    if (name == "east") {
        return SDL_GAMEPAD_BUTTON_EAST;
    }
    if (name == "west") {
        return SDL_GAMEPAD_BUTTON_WEST;
    }
    if (name == "north") {
        return SDL_GAMEPAD_BUTTON_NORTH;
    }
    if (name == "leftshoulder") {
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    }
    if (name == "rightshoulder") {
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    }
    if (name == "start") {
        return SDL_GAMEPAD_BUTTON_START;
    }
    if (name == "back") {
        return SDL_GAMEPAD_BUTTON_BACK;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view
gamepad_button_contract_name(const SDL_GamepadButton button) noexcept {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return "south";
    case SDL_GAMEPAD_BUTTON_EAST:
        return "east";
    case SDL_GAMEPAD_BUTTON_WEST:
        return "west";
    case SDL_GAMEPAD_BUTTON_NORTH:
        return "north";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return "leftshoulder";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return "rightshoulder";
    case SDL_GAMEPAD_BUTTON_START:
        return "start";
    case SDL_GAMEPAD_BUTTON_BACK:
        return "back";
    default:
        return {};
    }
}

[[nodiscard]] std::optional<InputMapError>
parse_keyboard_table(InputMap& map, const toml::table& table,
                     const std::filesystem::path& path) {
    for (const auto& [toml_key, node] : table) {
        const std::string_view key = toml_key.str();
        const auto action = action_from_key(key);
        if (!action) {
            return make_error(InputMapErrorCode::unknown_action, path, "keyboard",
                              key);
        }
        const auto value = node.value<std::string>();
        if (!value) {
            return make_error(InputMapErrorCode::invalid_value_type, path,
                              "keyboard", key);
        }
        const SDL_Scancode scancode = SDL_GetScancodeFromName(value->c_str());
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            return make_error(InputMapErrorCode::invalid_keyboard_name, path,
                              "keyboard", key, *value);
        }
        KeyboardBinding* const binding = InputMapBuilder::find_keyboard(map, *action);
        if (binding == nullptr) {
            return make_error(InputMapErrorCode::unknown_action, path, "keyboard",
                              key, *value);
        }
        binding->scancode = scancode;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<InputMapError>
parse_gamepad_table(InputMap& map, const toml::table& table,
                    const std::filesystem::path& path) {
    for (const auto& [toml_key, node] : table) {
        const std::string_view key = toml_key.str();
        const auto action = action_from_key(key);
        if (!action) {
            return make_error(InputMapErrorCode::unknown_action, path, "gamepad",
                              key);
        }
        const auto value = node.value<std::string>();
        if (!value) {
            return make_error(InputMapErrorCode::invalid_value_type, path,
                              "gamepad", key);
        }
        GamepadBinding* const binding = InputMapBuilder::find_gamepad(map, *action);
        if (binding == nullptr) {
            return make_error(InputMapErrorCode::unsupported_gamepad_action, path,
                              "gamepad", key, *value);
        }
        const auto button = gamepad_button_from_contract(*value);
        if (!button) {
            return make_error(InputMapErrorCode::invalid_gamepad_name, path,
                              "gamepad", key, *value);
        }
        binding->button = *button;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<InputMapError>
validate_unique_bindings(const InputMap& map,
                         const std::filesystem::path& path) {
    const auto keyboard = map.keyboard_bindings();
    for (std::size_t left = 0; left < keyboard.size(); ++left) {
        for (std::size_t right = left + 1; right < keyboard.size(); ++right) {
            if (keyboard[left].scancode == keyboard[right].scancode) {
                return make_error(
                    InputMapErrorCode::duplicate_keyboard_binding, path,
                    "keyboard", input_action_key(keyboard[right].action),
                    SDL_GetScancodeName(keyboard[right].scancode),
                    input_action_key(keyboard[left].action));
            }
        }
    }

    const auto gamepad = map.gamepad_bindings();
    for (std::size_t left = 0; left < gamepad.size(); ++left) {
        for (std::size_t right = left + 1; right < gamepad.size(); ++right) {
            if (gamepad[left].button == gamepad[right].button) {
                return make_error(
                    InputMapErrorCode::duplicate_gamepad_binding, path,
                    "gamepad", input_action_key(gamepad[right].action),
                    gamepad_button_contract_name(gamepad[right].button),
                    input_action_key(gamepad[left].action));
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] const char* error_summary(const InputMapErrorCode code) noexcept {
    switch (code) {
    case InputMapErrorCode::io_error:
        return "no se pudo leer el archivo";
    case InputMapErrorCode::parse_error:
        return "el TOML esta malformado";
    case InputMapErrorCode::unknown_section:
        return "la seccion superior no es valida";
    case InputMapErrorCode::unknown_action:
        return "la accion no existe en el contrato RetroPad";
    case InputMapErrorCode::unsupported_gamepad_action:
        return "la direccion del gamepad no es remapeable";
    case InputMapErrorCode::invalid_value_type:
        return "el binding debe ser un string";
    case InputMapErrorCode::invalid_keyboard_name:
        return "SDL no reconoce el nombre de tecla";
    case InputMapErrorCode::invalid_gamepad_name:
        return "el nombre de boton no pertenece al contrato";
    case InputMapErrorCode::duplicate_keyboard_binding:
        return "dos acciones usan la misma tecla";
    case InputMapErrorCode::duplicate_gamepad_binding:
        return "dos acciones usan el mismo boton";
    }
    return "el mapa no es valido";
}

}  // namespace

InputMap InputMap::defaults() noexcept {
    InputMap map;
    map.keyboard_ = {{
        {JoypadButton::up, SDL_SCANCODE_UP},
        {JoypadButton::down, SDL_SCANCODE_DOWN},
        {JoypadButton::left, SDL_SCANCODE_LEFT},
        {JoypadButton::right, SDL_SCANCODE_RIGHT},
        {JoypadButton::b, SDL_SCANCODE_Z},
        {JoypadButton::a, SDL_SCANCODE_X},
        {JoypadButton::y, SDL_SCANCODE_A},
        {JoypadButton::x, SDL_SCANCODE_S},
        {JoypadButton::l, SDL_SCANCODE_Q},
        {JoypadButton::r, SDL_SCANCODE_W},
        {JoypadButton::start, SDL_SCANCODE_RETURN},
        {JoypadButton::select, SDL_SCANCODE_RSHIFT},
    }};
    map.gamepad_ = {{
        {JoypadButton::b, SDL_GAMEPAD_BUTTON_SOUTH},
        {JoypadButton::a, SDL_GAMEPAD_BUTTON_EAST},
        {JoypadButton::y, SDL_GAMEPAD_BUTTON_WEST},
        {JoypadButton::x, SDL_GAMEPAD_BUTTON_NORTH},
        {JoypadButton::l, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
        {JoypadButton::r, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {JoypadButton::start, SDL_GAMEPAD_BUTTON_START},
        {JoypadButton::select, SDL_GAMEPAD_BUTTON_BACK},
    }};
    return map;
}

std::span<const KeyboardBinding> InputMap::keyboard_bindings() const noexcept {
    return keyboard_;
}

std::span<const GamepadBinding> InputMap::gamepad_bindings() const noexcept {
    return gamepad_;
}

SDL_Scancode
InputMap::keyboard_scancode(const JoypadButton action) const noexcept {
    const auto found = std::ranges::find_if(
        keyboard_, [action](const auto& binding) { return binding.action == action; });
    return found == keyboard_.end() ? SDL_SCANCODE_UNKNOWN : found->scancode;
}

std::optional<SDL_GamepadButton>
InputMap::gamepad_button(const JoypadButton action) const noexcept {
    const auto found = std::ranges::find_if(
        gamepad_, [action](const auto& binding) { return binding.action == action; });
    if (found == gamepad_.end()) {
        return std::nullopt;
    }
    return found->button;
}

InputMapLoadResult::InputMapLoadResult(InputMap map)
    : result_(std::move(map)) {}

InputMapLoadResult::InputMapLoadResult(InputMapError error)
    : result_(std::move(error)) {}

InputMap* InputMapLoadResult::map() noexcept {
    return std::get_if<InputMap>(&result_);
}

const InputMap* InputMapLoadResult::map() const noexcept {
    return std::get_if<InputMap>(&result_);
}

const InputMapError* InputMapLoadResult::error() const noexcept {
    return std::get_if<InputMapError>(&result_);
}

std::string_view input_action_key(const JoypadButton action) noexcept {
    switch (action) {
    case JoypadButton::up:
        return "up";
    case JoypadButton::down:
        return "down";
    case JoypadButton::left:
        return "left";
    case JoypadButton::right:
        return "right";
    case JoypadButton::b:
        return "b";
    case JoypadButton::a:
        return "a";
    case JoypadButton::y:
        return "y";
    case JoypadButton::x:
        return "x";
    case JoypadButton::l:
        return "l";
    case JoypadButton::r:
        return "r";
    case JoypadButton::start:
        return "start";
    case JoypadButton::select:
        return "select";
    case JoypadButton::l2:
    case JoypadButton::r2:
    case JoypadButton::l3:
    case JoypadButton::r3:
        return {};
    }
    return {};
}

InputMapLoadResult load_input_map(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return InputMapLoadResult{
            make_error(InputMapErrorCode::io_error, path)};
    }
    const std::string text{std::istreambuf_iterator<char>{file},
                           std::istreambuf_iterator<char>{}};
    if (file.bad()) {
        return InputMapLoadResult{
            make_error(InputMapErrorCode::io_error, path)};
    }

    toml::table document;
    try {
        document = toml::parse(text, path.string());
    } catch (const toml::parse_error& error) {
        return InputMapLoadResult{make_error(
            InputMapErrorCode::parse_error, path, {}, {}, {}, error.description())};
    }

    for (const auto& [key, node] : document) {
        const std::string_view section = key.str();
        if (section != "keyboard" && section != "gamepad") {
            return InputMapLoadResult{make_error(
                InputMapErrorCode::unknown_section, path, section)};
        }
        if (!node.is_table()) {
            return InputMapLoadResult{make_error(
                InputMapErrorCode::invalid_value_type, path, section)};
        }
    }

    InputMap map = InputMap::defaults();
    map.legacy_select_backspace_ = false;
    if (const toml::table* keyboard = document["keyboard"].as_table()) {
        if (auto error = parse_keyboard_table(map, *keyboard, path)) {
            return InputMapLoadResult{std::move(*error)};
        }
    }
    if (const toml::table* gamepad = document["gamepad"].as_table()) {
        if (auto error = parse_gamepad_table(map, *gamepad, path)) {
            return InputMapLoadResult{std::move(*error)};
        }
    }
    if (auto error = validate_unique_bindings(map, path)) {
        return InputMapLoadResult{std::move(*error)};
    }
    return InputMapLoadResult{std::move(map)};
}

std::string describe(const InputMapError& error) {
    std::string message = "--input-map '" + error.path.string() + "': ";
    message += error_summary(error.code);
    if (!error.section.empty()) {
        message += " [" + error.section + "]";
    }
    if (!error.key.empty()) {
        message += "." + error.key;
    }
    if (!error.value.empty()) {
        message += " ('" + error.value + "')";
    }
    if (!error.detail.empty()) {
        message += ": " + error.detail;
    }
    return message;
}

}  // namespace ayther
