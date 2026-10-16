#pragma once

#include "recording_header.h"
#include "recording_input_source.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

// Spec 006 (RF-2, RF-3): a pad timeline («guion de mando») is a text file that drives the
// game instead of a recorded take. One step per line, `#` starts a comment, keywords and
// button names are case-insensitive:
//
//   start power_on                 default when absent; the first step if present
//   start state <path>             a Lab bookmark (.sav), relative to the timeline file
//   wait <dur>                     frames pass with the held buttons
//   press <buttons> [<dur>]        held | buttons for <dur> (default_press_frames), then released
//   hold <buttons> <dur>           the same as press, with a mandatory duration
//   down <buttons>                 adds to the held mask; no frame passes
//   up <buttons>                   removes from the held mask; no frame passes
//
// <buttons> are Genesis names joined with `+`: a b c x y z start mode up down left right.
// <dur> is `<decimal>s`, `<decimal>ms` or `<integer>f`. The timeline is compiled to one
// RetroPad mask per frame with the fps the core reports, so everything downstream of a take
// (the replay loop, the navigation re-simulation, the input boundary) stays the same.
inline constexpr std::size_t max_timeline_bytes = 64U * 1024U;
inline constexpr std::size_t max_timeline_steps = 4096;
inline constexpr std::uint32_t default_press_frames = 4;
// The bounds the supervisor checks before admission, without the core: the fastest and the
// slowest timing a libretro core reports for the supported consoles.
inline constexpr double timeline_preflight_fps_max = 60.1;
inline constexpr double timeline_preflight_fps_min = 49.0;

// Genesis Plus GX maps the 3/6-button pad onto the RetroPad (libretro.c): A→Y, B→B, C→A,
// X→L, Y→X, Z→R, Mode→Select, Start→Start, and the d-pad onto itself.
struct GenesisButton {
    std::string_view name;
    std::uint16_t mask{};
};

[[nodiscard]] std::span<const GenesisButton> genesis_buttons() noexcept;

enum class TimelineStartKind { power_on, state };

struct TimelineStart {
    TimelineStartKind kind{TimelineStartKind::power_on};
    // As written in the file; empty for power_on.
    std::string state_path;
    bool operator==(const TimelineStart &) const = default;
};

enum class TimelineStepKind { wait, press, down, up };

enum class TimelineDurationUnit { seconds, frames };

struct TimelineDuration {
    TimelineDurationUnit unit{TimelineDurationUnit::frames};
    double seconds{};
    std::uint32_t frames{};
    bool operator==(const TimelineDuration &) const = default;
};

struct TimelineStep {
    TimelineStepKind kind{TimelineStepKind::wait};
    std::uint16_t buttons{};
    // Absent only for `down` and `up`; a `press` without one lasts default_press_frames.
    std::optional<TimelineDuration> duration;
    // 1-based line in the file, for the diagnostics.
    std::size_t line{};
    bool operator==(const TimelineStep &) const = default;
};

struct PadTimeline {
    TimelineStart start;
    std::vector<TimelineStep> steps;
    bool operator==(const PadTimeline &) const = default;
};

enum class TimelineParseError {
    none,
    file_too_large,
    unknown_keyword,
    unknown_button,
    invalid_duration,
    missing_duration,
    unexpected_argument,
    invalid_start,
    start_not_first,
    start_repeated,
    button_already_down,
    button_not_down,
    too_many_steps,
    no_steps,
};

struct TimelineParseResult {
    TimelineParseError error{TimelineParseError::none};
    // The line of the error, 1-based; 0 when the error has no line.
    std::size_t line{};
    PadTimeline timeline;
};

[[nodiscard]] TimelineParseResult parse_pad_timeline(std::string_view text);
[[nodiscard]] std::string_view timeline_parse_error_code(TimelineParseError error) noexcept;

// `a+start` → the RetroPad mask; nullopt for an unknown or repeated name.
[[nodiscard]] std::optional<std::uint16_t> parse_timeline_buttons(std::string_view text) noexcept;

enum class TimelineCompileError {
    none,
    invalid_fps,
    duration_rounds_to_zero,
    too_many_frames,
    no_frames,
};

struct CompiledTimeline {
    std::vector<std::uint16_t> buttons;
    // The same masks as little-endian u16, the layout of the inputs of a take.
    std::vector<std::byte> input_bytes;
    double fps{};
    std::uint32_t frames{};
};

struct TimelineCompileResult {
    TimelineCompileError error{TimelineCompileError::none};
    std::size_t line{};
    CompiledTimeline compiled;
};

// Seconds become frames with `llround(seconds × fps)`; a step that rounds to zero frames
// is an error, so no step is silently a no-op. The total stays within
// [min_recording_frames, max_recording_frames].
[[nodiscard]] TimelineCompileResult compile_pad_timeline(const PadTimeline &timeline, double fps);
[[nodiscard]] std::string_view timeline_compile_error_code(TimelineCompileError error) noexcept;

// The compiled inputs as a take layout and input source, so the replay loop reads them like
// the inputs of a recording. The source refers to `compiled.input_bytes`.
[[nodiscard]] RecordingLayout timeline_input_layout(const CompiledTimeline &compiled) noexcept;
[[nodiscard]] RecordingInputSourceResult
make_timeline_input_source(const CompiledTimeline &compiled) noexcept;

// `start state <path>` is resolved against the directory of the timeline file.
[[nodiscard]] std::filesystem::path
resolve_timeline_state_path(const std::filesystem::path &timeline_file, std::string_view written);
// `<state>.fm`: the 512-byte YM2612 register shadow the Lab may write next to a bookmark.
[[nodiscard]] std::filesystem::path timeline_fm_sidecar_path(const std::filesystem::path &state);
inline constexpr std::size_t timeline_fm_sidecar_bytes = 512;

} // namespace ayther::audio_qa
