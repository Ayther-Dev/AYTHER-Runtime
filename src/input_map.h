#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <ayther/engine/input.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace ayther {

inline constexpr std::size_t input_keyboard_binding_count = 12;
inline constexpr std::size_t input_gamepad_binding_count = 8;

struct KeyboardBinding {
    engine::JoypadButton action{};
    SDL_Scancode scancode{SDL_SCANCODE_UNKNOWN};
};

struct GamepadBinding {
    engine::JoypadButton action{};
    SDL_GamepadButton button{SDL_GAMEPAD_BUTTON_INVALID};
};

class InputMapLoadResult;
class InputMapBuilder;

/// A complete, startup-resolved mapping from SDL inputs to RetroPad actions.
class InputMap final {
public:
    [[nodiscard]] static InputMap defaults() noexcept;

    [[nodiscard]] std::span<const KeyboardBinding>
    keyboard_bindings() const noexcept;
    [[nodiscard]] std::span<const GamepadBinding>
    gamepad_bindings() const noexcept;

    [[nodiscard]] SDL_Scancode
    keyboard_scancode(engine::JoypadButton action) const noexcept;
    [[nodiscard]] std::optional<SDL_GamepadButton>
    gamepad_button(engine::JoypadButton action) const noexcept;

    /// Backspace was historically an additional Select key. It remains active
    /// only when no explicit input map was supplied.
    [[nodiscard]] bool legacy_select_backspace() const noexcept {
        return legacy_select_backspace_;
    }

private:
    friend class InputMapBuilder;
    friend class InputMapLoadResult;
    friend InputMapLoadResult load_input_map(const std::filesystem::path& path);

    std::array<KeyboardBinding, input_keyboard_binding_count> keyboard_{};
    std::array<GamepadBinding, input_gamepad_binding_count> gamepad_{};
    bool legacy_select_backspace_{true};
};

enum class InputMapErrorCode {
    io_error,
    parse_error,
    unknown_section,
    unknown_action,
    unsupported_gamepad_action,
    invalid_value_type,
    invalid_keyboard_name,
    invalid_gamepad_name,
    duplicate_keyboard_binding,
    duplicate_gamepad_binding,
};

struct InputMapError {
    InputMapErrorCode code{};
    std::filesystem::path path;
    std::string section;
    std::string key;
    std::string value;
    std::string detail;
};

class InputMapLoadResult final {
public:
    explicit InputMapLoadResult(InputMap map);
    explicit InputMapLoadResult(InputMapError error);

    [[nodiscard]] InputMap* map() noexcept;
    [[nodiscard]] const InputMap* map() const noexcept;
    [[nodiscard]] const InputMapError* error() const noexcept;

private:
    std::variant<InputMap, InputMapError> result_;
};

[[nodiscard]] std::string_view
input_action_key(engine::JoypadButton action) noexcept;
[[nodiscard]] InputMapLoadResult
load_input_map(const std::filesystem::path& path);
[[nodiscard]] std::string describe(const InputMapError& error);

}  // namespace ayther
