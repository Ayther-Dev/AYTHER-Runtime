#include "pad_timeline.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace ayther::audio_qa {
namespace {

constexpr std::array<GenesisButton, 12> buttons_table{{
    {"a", 0x0002},
    {"b", 0x0001},
    {"c", 0x0100},
    {"x", 0x0400},
    {"y", 0x0200},
    {"z", 0x0800},
    {"start", 0x0008},
    {"mode", 0x0004},
    {"up", 0x0010},
    {"down", 0x0020},
    {"left", 0x0040},
    {"right", 0x0080},
}};

// Only the seconds a replay can hold; anything longer is rejected before the arithmetic.
constexpr double max_step_seconds = 1'000'000.0;

bool is_space(const char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\f' ||
           character == '\v';
}

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

std::string lowered(std::string_view text) {
    std::string result{text};
    for (auto &character : result)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return result;
}

// `#` opens a comment at the start of the line or after a blank, outside double quotes, so
// a `#` inside a path stays part of it.
std::string_view without_comment(std::string_view line) noexcept {
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"')
            quoted = !quoted;
        else if (!quoted && line[index] == '#' && (index == 0U || is_space(line[index - 1U])))
            return line.substr(0, index);
    }
    return line;
}

// The next blank-separated token and the rest of the line.
std::pair<std::string_view, std::string_view> split_token(std::string_view text) noexcept {
    text = trim(text);
    std::size_t end = 0;
    while (end < text.size() && !is_space(text[end]))
        ++end;
    return {text.substr(0, end), trim(text.substr(end))};
}

std::string_view unquoted(std::string_view text) noexcept {
    if (text.size() >= 2U && text.front() == '"' && text.back() == '"')
        return text.substr(1U, text.size() - 2U);
    return text;
}

std::optional<TimelineDuration> parse_duration(std::string_view text) noexcept {
    const auto value = lowered(text);
    std::string_view number{value};
    TimelineDurationUnit unit{};
    double scale = 1.0;
    if (number.ends_with("ms")) {
        number.remove_suffix(2);
        unit = TimelineDurationUnit::seconds;
        scale = 0.001;
    } else if (number.ends_with("s")) {
        number.remove_suffix(1);
        unit = TimelineDurationUnit::seconds;
    } else if (number.ends_with("f")) {
        number.remove_suffix(1);
        unit = TimelineDurationUnit::frames;
    } else {
        return std::nullopt;
    }
    if (number.empty() || number.front() == '-' || number.front() == '+')
        return std::nullopt;
    const auto *end = number.data() + number.size();
    TimelineDuration duration{unit};
    if (unit == TimelineDurationUnit::frames) {
        std::uint32_t frames{};
        const auto parsed = std::from_chars(number.data(), end, frames);
        if (parsed.ec != std::errc{} || parsed.ptr != end || frames < min_recording_frames ||
            frames > max_recording_frames)
            return std::nullopt;
        duration.frames = frames;
        return duration;
    }
    double seconds{};
    const auto parsed = std::from_chars(number.data(), end, seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(seconds))
        return std::nullopt;
    seconds *= scale;
    if (seconds <= 0.0 || seconds > max_step_seconds)
        return std::nullopt;
    duration.seconds = seconds;
    return duration;
}

TimelineParseResult failure(const TimelineParseError error, const std::size_t line) {
    TimelineParseResult result;
    result.error = error;
    result.line = line;
    return result;
}

} // namespace

std::span<const GenesisButton> genesis_buttons() noexcept { return buttons_table; }

std::optional<std::uint16_t> parse_timeline_buttons(const std::string_view text) noexcept {
    std::uint16_t mask{};
    std::string_view rest = text;
    if (rest.empty())
        return std::nullopt;
    while (!rest.empty()) {
        const auto plus = rest.find('+');
        const auto name = lowered(rest.substr(0, plus));
        rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1U);
        std::uint16_t found{};
        for (const auto &button : buttons_table)
            if (button.name == name)
                found = button.mask;
        if (found == 0U || (mask & found) != 0U)
            return std::nullopt;
        mask = static_cast<std::uint16_t>(mask | found);
        // A trailing `+` names nothing.
        if (plus != std::string_view::npos && rest.empty())
            return std::nullopt;
    }
    return mask;
}

TimelineParseResult parse_pad_timeline(const std::string_view text) {
    if (text.size() > max_timeline_bytes)
        return failure(TimelineParseError::file_too_large, 0U);
    TimelineParseResult result;
    auto &timeline = result.timeline;
    bool start_seen = false;
    std::uint16_t held{};
    std::size_t line_number = 0;
    std::string_view remaining = text;
    while (!remaining.empty()) {
        ++line_number;
        const auto newline = remaining.find('\n');
        const auto raw = remaining.substr(0, newline);
        remaining =
            newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1U);
        const auto content = trim(without_comment(raw));
        if (content.empty())
            continue;
        const auto [keyword_text, arguments] = split_token(content);
        const auto keyword = lowered(keyword_text);

        if (keyword == "start") {
            if (start_seen)
                return failure(TimelineParseError::start_repeated, line_number);
            if (!timeline.steps.empty())
                return failure(TimelineParseError::start_not_first, line_number);
            start_seen = true;
            const auto [kind_text, path] = split_token(arguments);
            const auto kind = lowered(kind_text);
            if (kind == "power_on") {
                if (!path.empty())
                    return failure(TimelineParseError::unexpected_argument, line_number);
                timeline.start = {TimelineStartKind::power_on, {}};
                continue;
            }
            if (kind == "state") {
                const auto state = unquoted(path);
                if (state.empty())
                    return failure(TimelineParseError::invalid_start, line_number);
                timeline.start = {TimelineStartKind::state, std::string{state}};
                continue;
            }
            return failure(TimelineParseError::invalid_start, line_number);
        }

        if (timeline.steps.size() == max_timeline_steps)
            return failure(TimelineParseError::too_many_steps, line_number);
        TimelineStep step;
        step.line = line_number;
        if (keyword == "wait") {
            step.kind = TimelineStepKind::wait;
            const auto [duration_text, rest] = split_token(arguments);
            if (duration_text.empty())
                return failure(TimelineParseError::missing_duration, line_number);
            if (!rest.empty())
                return failure(TimelineParseError::unexpected_argument, line_number);
            step.duration = parse_duration(duration_text);
            if (!step.duration)
                return failure(TimelineParseError::invalid_duration, line_number);
            timeline.steps.push_back(std::move(step));
            continue;
        }
        const bool press = keyword == "press";
        const bool hold = keyword == "hold";
        const bool down = keyword == "down";
        const bool up = keyword == "up";
        if (!press && !hold && !down && !up)
            return failure(TimelineParseError::unknown_keyword, line_number);
        const auto [buttons_text, after_buttons] = split_token(arguments);
        if (buttons_text.empty())
            return failure(TimelineParseError::unknown_button, line_number);
        const auto buttons = parse_timeline_buttons(buttons_text);
        if (!buttons)
            return failure(TimelineParseError::unknown_button, line_number);
        step.buttons = *buttons;
        const auto [duration_text, rest] = split_token(after_buttons);
        if (!rest.empty())
            return failure(TimelineParseError::unexpected_argument, line_number);
        if (press || hold) {
            step.kind = TimelineStepKind::press;
            if (duration_text.empty()) {
                if (hold)
                    return failure(TimelineParseError::missing_duration, line_number);
                step.duration =
                    TimelineDuration{TimelineDurationUnit::frames, 0.0, default_press_frames};
            } else {
                step.duration = parse_duration(duration_text);
                if (!step.duration)
                    return failure(TimelineParseError::invalid_duration, line_number);
            }
            timeline.steps.push_back(std::move(step));
            continue;
        }
        if (!duration_text.empty())
            return failure(TimelineParseError::unexpected_argument, line_number);
        if (down) {
            if ((held & step.buttons) != 0U)
                return failure(TimelineParseError::button_already_down, line_number);
            held = static_cast<std::uint16_t>(held | step.buttons);
            step.kind = TimelineStepKind::down;
        } else {
            if ((held & step.buttons) != step.buttons)
                return failure(TimelineParseError::button_not_down, line_number);
            held = static_cast<std::uint16_t>(held & ~step.buttons);
            step.kind = TimelineStepKind::up;
        }
        timeline.steps.push_back(std::move(step));
    }
    if (timeline.steps.empty())
        return failure(TimelineParseError::no_steps, 0U);
    return result;
}

std::string_view timeline_parse_error_code(const TimelineParseError error) noexcept {
    switch (error) {
    case TimelineParseError::none:
        return "none";
    case TimelineParseError::file_too_large:
        return "file_too_large";
    case TimelineParseError::unknown_keyword:
        return "unknown_keyword";
    case TimelineParseError::unknown_button:
        return "unknown_button";
    case TimelineParseError::invalid_duration:
        return "invalid_duration";
    case TimelineParseError::missing_duration:
        return "missing_duration";
    case TimelineParseError::unexpected_argument:
        return "unexpected_argument";
    case TimelineParseError::invalid_start:
        return "invalid_start";
    case TimelineParseError::start_not_first:
        return "start_not_first";
    case TimelineParseError::start_repeated:
        return "start_repeated";
    case TimelineParseError::button_already_down:
        return "button_already_down";
    case TimelineParseError::button_not_down:
        return "button_not_down";
    case TimelineParseError::too_many_steps:
        return "too_many_steps";
    case TimelineParseError::no_steps:
        return "no_steps";
    }
    return "invalid_timeline";
}

TimelineCompileResult compile_pad_timeline(const PadTimeline &timeline, const double fps) {
    TimelineCompileResult result;
    result.compiled.fps = fps;
    if (!std::isfinite(fps) || fps <= 0.0 || fps > 1000.0) {
        result.error = TimelineCompileError::invalid_fps;
        return result;
    }
    auto &buttons = result.compiled.buttons;
    std::uint16_t held{};
    for (std::size_t index = 0; index < timeline.steps.size(); ++index) {
        const auto &step = timeline.steps[index];
        if (step.kind == TimelineStepKind::down) {
            held = static_cast<std::uint16_t>(held | step.buttons);
            continue;
        }
        if (step.kind == TimelineStepKind::up) {
            held = static_cast<std::uint16_t>(held & ~step.buttons);
            continue;
        }
        const auto duration = step.duration.value_or(
            TimelineDuration{TimelineDurationUnit::frames, 0.0, default_press_frames});
        std::uint64_t frames{};
        if (duration.unit == TimelineDurationUnit::frames) {
            frames = duration.frames;
        } else {
            const auto rounded = std::llround(duration.seconds * fps);
            frames = rounded <= 0 ? 0U : static_cast<std::uint64_t>(rounded);
        }
        if (frames == 0U) {
            result.error = TimelineCompileError::duration_rounds_to_zero;
            result.line = step.line;
            return result;
        }
        if (buttons.size() + frames > max_recording_frames) {
            result.error = TimelineCompileError::too_many_frames;
            result.line = step.line;
            return result;
        }
        const auto mask = step.kind == TimelineStepKind::press
                              ? static_cast<std::uint16_t>(held | step.buttons)
                              : held;
        result.compiled.spans.push_back({index, static_cast<std::uint32_t>(buttons.size()),
                                         static_cast<std::uint32_t>(frames)});
        buttons.insert(buttons.end(), static_cast<std::size_t>(frames), mask);
    }
    if (buttons.size() < min_recording_frames) {
        result.error = TimelineCompileError::no_frames;
        return result;
    }
    result.compiled.frames = static_cast<std::uint32_t>(buttons.size());
    auto &bytes = result.compiled.input_bytes;
    bytes.reserve(buttons.size() * sizeof(std::uint16_t));
    for (const auto mask : buttons) {
        bytes.push_back(static_cast<std::byte>(mask & 0xffU));
        bytes.push_back(static_cast<std::byte>(mask >> 8U));
    }
    return result;
}

std::optional<TimelinePosition> timeline_position(const CompiledTimeline &compiled,
                                                  const std::uint32_t frame) noexcept {
    for (std::size_t index = 0; index < compiled.spans.size(); ++index) {
        const auto &span = compiled.spans[index];
        if (frame >= span.first_frame && frame - span.first_frame < span.frames)
            return TimelinePosition{index, span.step, span.first_frame + span.frames - frame};
    }
    return std::nullopt;
}

std::string describe_timeline_buttons(const std::uint16_t mask) {
    std::string names;
    for (const auto &button : genesis_buttons()) {
        if ((mask & button.mask) == 0U)
            continue;
        if (!names.empty())
            names.push_back('+');
        names.append(button.name);
    }
    return names.empty() ? std::string{"-"} : names;
}

std::string describe_timeline_step(const TimelineStep &step) {
    std::string text;
    switch (step.kind) {
    case TimelineStepKind::wait:
        text = "wait";
        break;
    case TimelineStepKind::press:
        text = "press";
        break;
    case TimelineStepKind::down:
        text = "down";
        break;
    case TimelineStepKind::up:
        text = "up";
        break;
    }
    if (step.kind != TimelineStepKind::wait)
        text += " " + describe_timeline_buttons(step.buttons);
    if (step.duration) {
        if (step.duration->unit == TimelineDurationUnit::frames) {
            text += " " + std::to_string(step.duration->frames) + "f";
        } else {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%gs", step.duration->seconds);
            text += " ";
            text += buffer;
        }
    }
    return text;
}

std::string_view timeline_compile_error_code(const TimelineCompileError error) noexcept {
    switch (error) {
    case TimelineCompileError::none:
        return "none";
    case TimelineCompileError::invalid_fps:
        return "invalid_fps";
    case TimelineCompileError::duration_rounds_to_zero:
        return "duration_rounds_to_zero";
    case TimelineCompileError::too_many_frames:
        return "too_many_frames";
    case TimelineCompileError::no_frames:
        return "no_frames";
    }
    return "invalid_timeline";
}

RecordingLayout timeline_input_layout(const CompiledTimeline &compiled) noexcept {
    RecordingLayout layout;
    layout.header.version = current_recording_version;
    layout.frame_count = compiled.frames;
    layout.inputs = {0U, static_cast<std::uint64_t>(compiled.frames) * sizeof(std::uint16_t)};
    return layout;
}

RecordingInputSourceResult make_timeline_input_source(const CompiledTimeline &compiled) noexcept {
    return make_recording_input_source(compiled.input_bytes, timeline_input_layout(compiled));
}

std::filesystem::path resolve_timeline_state_path(const std::filesystem::path &timeline_file,
                                                  const std::string_view written) {
    std::filesystem::path state{std::string{written}};
    if (state.is_absolute())
        return state.lexically_normal();
    return (timeline_file.parent_path() / state).lexically_normal();
}

std::filesystem::path timeline_fm_sidecar_path(const std::filesystem::path &state) {
    auto sidecar = state;
    sidecar.replace_extension(".fm");
    return sidecar;
}

} // namespace ayther::audio_qa
