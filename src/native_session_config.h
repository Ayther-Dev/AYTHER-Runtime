#pragma once
// Spec 008 (RF-5.3, TP-304.2): the AytherSession configuration of the Tessera
// provider for a Mega Drive ROM. The layout is Tessera's wire format, as in the
// Engine's tools/parity_bench/native_config.h: system 1, the backend and its
// build id, 48 kHz audio, two pads of twelve controls and the SHA-256 of the
// initial persistent memory. The ROM travels as bytes, never as a path.

#include <ayther/engine/native_emulation.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ayther::runtime {

inline constexpr std::size_t native_persistent_memory_bytes = 65536;

struct NativeSessionConfigResult {
    bool ok{};
    ayther::engine::NativeConfig config;
    /// Stable reason when !ok: "core.unsupported_system" (the bytes are not a
    /// Mega Drive ROM), "native.unavailable" (no megadrive backend in this
    /// Engine) or "native.config_failed".
    std::string reason;
    std::string message;
};

/// Builds the megadrive configuration for `content`. `memory` is the initial
/// persistent memory; empty means native_persistent_memory_bytes of zeros.
[[nodiscard]] NativeSessionConfigResult
megadrive_session_config(std::vector<std::uint8_t> content, std::vector<std::uint8_t> memory);

} // namespace ayther::runtime
