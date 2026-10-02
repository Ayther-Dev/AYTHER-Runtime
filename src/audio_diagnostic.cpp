#include "audio_diagnostic.h"

#include <utility>

namespace ayther::runtime {

bool valid_audio_diagnostic(const AudioDiagnostic &value) noexcept {
    return !value.logical_identity.empty() && !value.track.empty() && !value.cause.empty() &&
           !value.runtime_version.empty() && !value.engine_version.empty();
}

AudioDiagnosticAggregator::AudioDiagnosticAggregator(const std::uint64_t minimum_period_ns)
    : minimum_period_ns_(minimum_period_ns) {}

std::string AudioDiagnosticAggregator::key(const AudioDiagnostic &value) {
    std::string result{audio_diagnostic_code(value.code)};
    result.push_back('\0');
    result.append(value.logical_identity);
    result.push_back('\0');
    result.append(value.cause);
    return result;
}

std::optional<AudioDiagnosticAggregate>
AudioDiagnosticAggregator::push(const AudioDiagnostic &value) {
    if (!valid_audio_diagnostic(value))
        return std::nullopt;
    auto [position, inserted] = states_.try_emplace(key(value));
    State &state = position->second;
    if (inserted) {
        state.last = value;
        state.last_emit_ns = value.monotonic_ns;
        return AudioDiagnosticAggregate{value, 1U, 0U};
    }
    if (value.monotonic_ns < state.last_emit_ns) {
        state.last = value;
        state.last_emit_ns = value.monotonic_ns;
        state.pending = 0U;
        return AudioDiagnosticAggregate{value, 1U, 0U};
    }
    state.last = value;
    const std::uint64_t elapsed = value.monotonic_ns - state.last_emit_ns;
    if (elapsed < minimum_period_ns_) {
        ++state.pending;
        return std::nullopt;
    }
    const std::uint64_t count = state.pending + 1U;
    state.pending = 0U;
    state.last_emit_ns = value.monotonic_ns;
    return AudioDiagnosticAggregate{value, count, elapsed};
}

std::vector<AudioDiagnosticAggregate>
AudioDiagnosticAggregator::close(const std::uint64_t monotonic_ns) {
    std::vector<AudioDiagnosticAggregate> result;
    result.reserve(states_.size());
    for (auto &[identity, state] : states_) {
        (void)identity;
        if (state.pending == 0U)
            continue;
        const std::uint64_t elapsed =
            monotonic_ns >= state.last_emit_ns ? monotonic_ns - state.last_emit_ns : 0U;
        result.push_back({state.last, state.pending, elapsed});
        state.pending = 0U;
        state.last_emit_ns = monotonic_ns;
    }
    return result;
}

} // namespace ayther::runtime
