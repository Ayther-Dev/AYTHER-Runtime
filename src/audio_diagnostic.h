#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ayther::runtime {

enum class AudioDiagnosticCode {
    unexpected_restart,
    window_end,
    invalid_file,
    output_failure,
};

[[nodiscard]] constexpr std::string_view audio_diagnostic_code(AudioDiagnosticCode code) noexcept {
    switch (code) {
    case AudioDiagnosticCode::unexpected_restart:
        return "audio.unexpected_restart";
    case AudioDiagnosticCode::window_end:
        return "audio.window_end";
    case AudioDiagnosticCode::invalid_file:
        return "audio.invalid_file";
    case AudioDiagnosticCode::output_failure:
        return "audio.output_failure";
    }
    return "audio.unknown";
}

struct AudioDiagnostic {
    AudioDiagnosticCode code{AudioDiagnosticCode::output_failure};
    std::string logical_identity;
    std::string track;
    std::string cause;
    std::string runtime_version;
    std::string engine_version;
    std::uint64_t monotonic_ns{};
};

[[nodiscard]] bool valid_audio_diagnostic(const AudioDiagnostic &value) noexcept;

struct AudioDiagnosticAggregate {
    AudioDiagnostic diagnostic;
    std::uint64_t count{};
    std::uint64_t period_ns{};
};

class AudioDiagnosticAggregator final {
  public:
    explicit AudioDiagnosticAggregator(std::uint64_t minimum_period_ns);
    [[nodiscard]] std::optional<AudioDiagnosticAggregate> push(const AudioDiagnostic &value);
    [[nodiscard]] std::vector<AudioDiagnosticAggregate> close(std::uint64_t monotonic_ns);

  private:
    struct State {
        AudioDiagnostic last;
        std::uint64_t last_emit_ns{};
        std::uint64_t pending{};
    };
    [[nodiscard]] static std::string key(const AudioDiagnostic &value);

    std::uint64_t minimum_period_ns_{};
    std::unordered_map<std::string, State> states_;
};

} // namespace ayther::runtime
