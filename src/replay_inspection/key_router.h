#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace ayther::replay_inspection {

// Spec 002, plan §5.5 (RF-3.2, RF-4.5, RF-4.7, RF-4.9, RF-5.9, RF-5.10, RF-6.4, RF-6.5).
// Only Space, ←, → and I are considered; everything else is discarded and never reaches
// the game. ↑ and ↓ are known so that they are never mistaken for navigation.
enum class Key { space, left, right, up, down, info, other };
enum class KeyAction { toggle, left, right, info };

class KeyRouter final {
  public:
    // At most one action per physical press. `captured` is true when the focused control
    // of the window takes the key; the overlay never captures Space, arrows or I.
    [[nodiscard]] std::optional<KeyAction> key_down(Key key, bool repeat, bool captured = false);
    void key_up(Key key) noexcept;
    // Losing focus stops every action; on regaining it, every key still held is blocked
    // until it is released.
    void focus(bool focused) noexcept;

  private:
    static constexpr std::size_t key_count = 7;
    [[nodiscard]] static std::size_t index(Key key) noexcept {
        return static_cast<std::size_t>(key);
    }

    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> blocked_{};
    bool focused_{true};
    // ← and → were held together: no arrow moves until both are released.
    bool arrows_locked_{};
};

} // namespace ayther::replay_inspection
