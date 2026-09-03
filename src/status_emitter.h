#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>

namespace ayther::runtime {

struct ProbeSucceededStatus {
    std::uint32_t api{};
    std::string library_name;
    std::string library_version;
    std::string valid_extensions;
    bool need_fullpath{};
    bool block_extract{};
};

struct ProbeFailedStatus {
    std::string reason;
};

struct ReadyStatus {
    std::string game_id;
    bool has_pack{};
    std::string manifest;
};

struct NowPlayingStatus {
    std::string game_id;
    std::string title;
};

struct WarningStatus {
    std::string reason;
};

struct CrashTestStatus {};

struct ExitStatus {
    std::optional<std::string> savestate;
};

using StatusEvent =
    std::variant<ProbeSucceededStatus, ProbeFailedStatus, ReadyStatus, NowPlayingStatus,
                 WarningStatus, CrashTestStatus, ExitStatus>;

class StatusEmitter final {
public:
    explicit StatusEmitter(std::FILE& output) noexcept;

    /// Formats the complete line-delimited process record, including prefix
    /// and trailing newline. All event text is encoded by the same JSON writer.
    [[nodiscard]] static std::string format_line(const StatusEvent& event);

    /// Writes one complete record with one fwrite call, then flushes the stream.
    [[nodiscard]] bool emit(const StatusEvent& event) const;

private:
    std::FILE* output_;
};

}  // namespace ayther::runtime
