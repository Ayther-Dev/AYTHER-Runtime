#pragma once
// ---------------------------------------------------------------------------
// pause_dialog_options.h — what the launcher may customise in the pause dialog.
//
// The launcher that spawns the Runtime passes these through the CLI
// (--accent-color, --title, --exit-button-text); without them the dialog shows
// the Runtime's own defaults.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ayther {

struct PauseDialogOptions {
    /// Accent colour as 0xRRGGBB (default #FB2C36).
    std::uint32_t accent_rgb = 0xFB2C36U;
    /// Small accent-coloured caption above «EN PAUSA».
    std::string title = "Runtime";
    /// Label of the button that closes the Runtime.
    std::string exit_button_text = "Salir";
};

/// Parses `#RRGGBB` (the leading `#` is optional; hex digits in any case).
/// @return the colour as 0xRRGGBB, or no value for any other spelling.
[[nodiscard]] inline std::optional<std::uint32_t> parse_hex_rgb(std::string_view text) noexcept {
    if (text.starts_with('#'))
        text.remove_prefix(1);
    if (text.size() != 6)
        return std::nullopt;
    std::uint32_t value = 0;
    for (const char c : text) {
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return std::nullopt;
        value = (value << 4U) | digit;
    }
    return value;
}

} // namespace ayther
