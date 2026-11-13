#include "native_session_config.h"

#include <ayther/ayther_core_ffi.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace ayther::runtime {
namespace {

using Bytes = std::vector<std::uint8_t>;
namespace native = ayther::engine;

constexpr std::uint32_t kSystemMegaDrive = 1; // TESSERA_SYSTEM_MEGA_DRIVE
constexpr std::uint32_t kControlsPerPad = 12;
constexpr std::uint32_t kPads = 2;

void put(Bytes &out, const std::uint64_t value, const unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        out.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
}

void field(Bytes &out, const std::uint32_t tag, const std::uint32_t encoding,
           const std::span<const std::uint8_t> data) {
    put(out, tag, 4);
    put(out, encoding, 4);
    put(out, data.size(), 8);
    out.insert(out.end(), data.begin(), data.end());
}

void scalar(Bytes &out, const std::uint32_t tag, const std::uint32_t encoding,
            const std::uint32_t value) {
    Bytes data;
    put(data, value, 4);
    field(out, tag, encoding, data);
}

int nibble(const char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

NativeSessionConfigResult failure(std::string reason, std::string message) {
    NativeSessionConfigResult result;
    result.reason = std::move(reason);
    result.message = std::move(message);
    return result;
}

} // namespace

NativeSessionConfigResult megadrive_session_config(Bytes content, Bytes memory) {
    if (content.empty())
        return failure("native.config_failed", "la ROM esta vacia");
    if (memory.empty())
        memory.resize(native_persistent_memory_bytes);

    native::NativeDetection detection;
    const native::NativeResult detected = native::NativeEmulation::detect(content, 0, detection);
    if (detected.code != 0)
        return failure("native.unavailable",
                       "el Engine no pudo detectar el sistema con Tessera (codigo " +
                           std::to_string(detected.code) + ")");
    if (detection.system != kSystemMegaDrive)
        return failure("core.unsupported_system",
                       "la ROM no es de Mega Drive; Tessera no la emula todavia");

    std::vector<native::NativeBackendInfo> backends;
    if (native::NativeEmulation::backends(backends).code != 0)
        return failure("native.unavailable", "este Engine no incluye el proveedor Tessera");
    const auto found = std::ranges::find(backends, std::string_view{"megadrive"},
                                         &native::NativeBackendInfo::id);
    if (found == backends.end())
        return failure("native.unavailable", "el Tessera instalado no tiene el backend megadrive");

    NativeSessionConfigResult result;
    native::NativeConfig &config = result.config;
    config.backend_id = found->id;
    config.content = std::move(content);
    config.initial_persistent_memory = std::move(memory);
    config.system_override = kSystemMegaDrive;
    scalar(config.config, 1, 1, kSystemMegaDrive);
    field(config.config, 2, 6, Bytes(config.backend_id.begin(), config.backend_id.end()));
    field(config.config, 3, 5, found->build_id);
    scalar(config.config, 4, 1, kSystemMegaDrive);
    scalar(config.config, 5, 1, 48000);
    Bytes controls;
    put(controls, kPads * kControlsPerPad, 8);
    for (std::uint32_t index = 0; index < kPads * kControlsPerPad; ++index) {
        Bytes entry;
        scalar(entry, 1, 1, index / kControlsPerPad);
        scalar(entry, 2, 1, index % kControlsPerPad);
        scalar(entry, 3, 1, 1);
        scalar(entry, 4, 2, 0);
        scalar(entry, 5, 2, 1);
        put(controls, entry.size(), 8);
        controls.insert(controls.end(), entry.begin(), entry.end());
    }
    field(config.config, 6, 8, controls);
    std::array<char, 65> digest{};
    if (!ayther_sha256_hex(config.initial_persistent_memory.data(),
                           config.initial_persistent_memory.size(), digest.data()))
        return failure("native.config_failed", "no se pudo calcular el SHA-256 de la memoria");
    Bytes hash(32);
    for (std::size_t i = 0; i < hash.size(); ++i) {
        const int hi = nibble(digest[i * 2]);
        const int lo = nibble(digest[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return failure("native.config_failed", "SHA-256 invalido");
        hash[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    field(config.config, 7, 5, hash);
    result.ok = true;
    return result;
}

} // namespace ayther::runtime
