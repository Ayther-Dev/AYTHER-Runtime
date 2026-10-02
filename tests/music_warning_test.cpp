#include "music_warning.h"

#include <cstdio>
#include <string>

int main() {
    using namespace ayther::runtime;
    int failures = 0;
    const auto check = [&failures](bool value, const char *message) {
        std::printf("  [%s] %s\n", value ? "ok" : "FAIL", message);
        failures += value ? 0 : 1;
    };

    for (const auto code :
         {MusicWarningCode::analysis_obsolete, MusicWarningCode::game_pause_unobservable,
          MusicWarningCode::analysis_limit, MusicWarningCode::incompatible_pack,
          MusicWarningCode::restore_failed_rolled_back}) {
        const std::string technical{music_warning_code(code)};
        check(music_warning_message(code, "es", "Runtime").find(technical) != std::string::npos,
              "Spanish warning retains technical code");
        check(music_warning_message(code, "en", "Runtime").find(technical) != std::string::npos,
              "English warning retains technical code");
    }

    MusicWarningAggregator warnings{1'000'000'000ULL};
    MusicWarning event{MusicWarningCode::analysis_limit, "music:stage-1", "candidate budget", 100};
    check(warnings.push(event).has_value(), "first logical warning emits immediately");
    event.monotonic_ns = 200;
    check(!warnings.push(event), "same persistent condition is rate limited");
    event.monotonic_ns = 1'000'000'100ULL;
    const auto aggregate = warnings.push(event);
    check(aggregate && aggregate->count == 2 && aggregate->first_ns == 200 &&
              aggregate->last_ns == 1'000'000'100ULL && aggregate->detail == "candidate budget",
          "aggregate retains count first last and detail");
    event.monotonic_ns += 1;
    check(!warnings.push(event), "tail remains pending");
    const auto tail = warnings.close(event.monotonic_ns);
    check(tail.size() == 1 && tail.front().count == 1, "close emits pending remainder");
    check(warnings.full_trace().size() == 4,
          "full trace is never suppressed by warning aggregation");
    return failures == 0 ? 0 : 1;
}
