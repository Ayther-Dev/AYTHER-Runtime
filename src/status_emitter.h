#pragma once

#include "audio_diagnostic.h"
#include "runtime_error.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::runtime {

inline constexpr std::uint32_t status_protocol_version = 1;

// Appends `value` as a JSON string: quotes, reverse slashes and control bytes escaped.
void append_json_string(std::string &output, std::string_view value);

enum class StatusProtocolCompatibility {
    compatible,
    unsupported_older,
    unsupported_newer,
};

[[nodiscard]] constexpr StatusProtocolCompatibility
status_protocol_compatibility(const std::uint32_t peer_version) noexcept {
    if (peer_version == status_protocol_version) {
        return StatusProtocolCompatibility::compatible;
    }
    return peer_version < status_protocol_version ? StatusProtocolCompatibility::unsupported_older
                                                  : StatusProtocolCompatibility::unsupported_newer;
}

// Spec 002 (contracts.md C5, «Sondeo del core con la ROM»): with --probe-rom the probe
// also says whether the core loaded the ROM and with which timing and geometry. The
// geometry is the one the Engine reports before the first frame, when it reports one.
struct ProbeGameStatus {
    bool loaded{};
    double timing_fps{};
    bool geometry_known{};
    std::uint32_t geometry_width{};
    std::uint32_t geometry_height{};
    std::string message;
};

struct ProbeSucceededStatus {
    std::uint32_t api{};
    std::string library_name;
    std::string library_version;
    std::string valid_extensions;
    bool need_fullpath{};
    bool block_extract{};
    std::optional<ProbeGameStatus> game{};
};

struct ProbeFailedStatus {
    RuntimeErrorCode code{RuntimeErrorCode::core_load_failed};
    std::string message;
};

// Spec 008 (RF-9.3, TP-304.5): the probe contract v2 line, serialized by the
// Engine (`ProbeReport::serialize`), extended with the Libretro metadata and the
// ROM probe Play and the audio QA tools read. Fields are only added: every
// field of the contract keeps its name and value.
struct ProbeReportStatus {
    std::string report_json;
    bool loaded{};
    bool has_libretro_info{};
    std::string valid_extensions;
    bool need_fullpath{};
    bool block_extract{};
    std::optional<ProbeGameStatus> game;
};

struct ReadyStatus {
    std::string game_id;
    bool has_pack{};
    std::string manifest;
    // Spec 008 (RF-6.1): emulator generation of the session (`AytherSession::
    // emulation_generation()`). Empty `emulator_id` = the Engine reported none,
    // and the fields are left out of the record.
    std::string emulator_id{};
    std::string emulator_version{};
    std::uint32_t determinism_contract{};
};

struct NowPlayingStatus {
    std::string game_id;
    std::string title;
};

struct WarningStatus {
    RuntimeErrorCode code{RuntimeErrorCode::vulkan_unavailable};
    std::string message;
};

struct AudioWarningStatus {
    AudioDiagnosticAggregate aggregate;
    std::string message;
};

struct CrashTestStatus {};

struct ExitStatus {
    std::optional<std::string> savestate;
};

using StatusEvent =
    std::variant<ProbeSucceededStatus, ProbeFailedStatus, ProbeReportStatus, ReadyStatus,
                 NowPlayingStatus,
                 WarningStatus, AudioWarningStatus, CrashTestStatus, ExitStatus>;

class StatusEmitter final {
  public:
    explicit StatusEmitter(std::FILE &output) noexcept;

    /// Formats the complete line-delimited process record, including prefix
    /// and trailing newline. All event text is encoded by the same JSON writer.
    [[nodiscard]] static std::string format_line(const StatusEvent &event);

    /// Writes one complete record with one fwrite call, then flushes the stream.
    [[nodiscard]] bool emit(const StatusEvent &event) const;

  private:
    std::FILE *output_;
};

} // namespace ayther::runtime
