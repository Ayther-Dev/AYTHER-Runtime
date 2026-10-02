#include "audio_diagnostic.h"

#include <cstdio>
#include <string>

namespace {
int failures{};
void check(bool value, const char *message) {
    std::printf("  [%s] %s\n", value ? "ok" : "FAIL", message);
    if (!value)
        ++failures;
}
} // namespace

int main() {
    using namespace ayther::runtime;
    check(audio_diagnostic_code(AudioDiagnosticCode::unexpected_restart) ==
              "audio.unexpected_restart",
          "restart has a stable code");
    check(audio_diagnostic_code(AudioDiagnosticCode::window_end) == "audio.window_end",
          "window end has a distinct code");
    check(audio_diagnostic_code(AudioDiagnosticCode::invalid_file) == "audio.invalid_file",
          "invalid file has a distinct code");
    check(audio_diagnostic_code(AudioDiagnosticCode::output_failure) == "audio.output_failure",
          "output failure has a distinct code");

    AudioDiagnostic event;
    event.code = AudioDiagnosticCode::unexpected_restart;
    event.logical_identity = "music:0x05d714c8e7a9ba44";
    event.track = "OST - Wilderness";
    event.cause = "same_key_created_new_occurrence";
    event.runtime_version = "0.1.0-beta.6+rf17";
    event.engine_version = "0.1.0+rf17";
    event.monotonic_ns = 100;
    check(valid_audio_diagnostic(event), "complete diagnostic is valid");
    check(!valid_audio_diagnostic(AudioDiagnostic{}), "missing context is rejected");

    AudioDiagnosticAggregator aggregator{1'000'000'000ULL};
    const auto first = aggregator.push(event);
    event.monotonic_ns += 100'000'000ULL;
    const auto second = aggregator.push(event);
    event.monotonic_ns += 900'000'000ULL;
    const auto third = aggregator.push(event);
    check(first && first->count == 1 && !second && third && third->count == 2,
          "persistent condition emits no more than one aggregate per second");
    check(third && third->period_ns == 1'000'000'000ULL,
          "aggregate preserves count and monotonic period");

    event.logical_identity = "voice:other";
    event.code = AudioDiagnosticCode::output_failure;
    event.monotonic_ns += 1;
    check(aggregator.push(event).has_value(), "different cause/identity emits independently");

    AudioDiagnostic simultaneous = event;
    simultaneous.code = AudioDiagnosticCode::invalid_file;
    simultaneous.cause = "decoder_rejected_header";
    check(aggregator.push(simultaneous).has_value(),
          "simultaneous distinct cause is aggregated independently");

    AudioDiagnostic changed_cause = event;
    changed_cause.cause = "device_removed";
    check(aggregator.push(changed_cause).has_value(),
          "cause change starts a separate logical condition");

    AudioDiagnostic tail = event;
    tail.logical_identity = "effect:tail";
    tail.monotonic_ns += 1;
    check(aggregator.push(tail).has_value(), "first tail occurrence emits");
    tail.monotonic_ns += 1;
    check(!aggregator.push(tail), "repeated tail occurrence is pending");
    const auto closed = aggregator.close(tail.monotonic_ns);
    check(closed.size() == 1 && closed.front().count == 1,
          "close preserves the pending remainder without inventing frames");

    AudioDiagnostic rollback = event;
    rollback.logical_identity = "music:clock";
    rollback.monotonic_ns = 2'000'000'000ULL;
    check(aggregator.push(rollback).has_value(), "clock fixture emits first event");
    rollback.monotonic_ns = 1'000'000'000ULL;
    const auto after_rollback = aggregator.push(rollback);
    check(after_rollback && after_rollback->count == 1 && after_rollback->period_ns == 0,
          "monotonic rollback resets aggregation instead of hiding an event");

    return failures == 0 ? 0 : 1;
}
