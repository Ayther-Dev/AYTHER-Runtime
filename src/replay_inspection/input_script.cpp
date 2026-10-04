#include "input_script.h"

#include <array>
#include <charconv>
#include <utility>

namespace ayther::replay_inspection {
namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
            ++index;
        const auto start = index;
        while (index < line.size() && line[index] != ' ' && line[index] != '\t')
            ++index;
        if (index > start)
            result.push_back(line.substr(start, index - start));
    }
    return result;
}

std::optional<std::uint64_t> number(std::string_view text) {
    std::uint64_t value{};
    const auto *end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || last != end)
        return std::nullopt;
    return value;
}

std::optional<ScriptTrigger> trigger(std::string_view text) {
    constexpr std::array<std::pair<std::string_view, ScriptTriggerKind>, 3> kinds{{
        {"frame=", ScriptTriggerKind::frame},
        {"paused=", ScriptTriggerKind::paused},
        {"after=", ScriptTriggerKind::after_ms},
    }};
    for (const auto &[prefix, kind] : kinds)
        if (text.starts_with(prefix))
            if (const auto value = number(text.substr(prefix.size())))
                return ScriptTrigger{kind, *value};
    return std::nullopt;
}

std::optional<Key> key(std::string_view text) {
    constexpr std::array<std::pair<std::string_view, Key>, 7> keys{{
        {"space", Key::space},
        {"left", Key::left},
        {"right", Key::right},
        {"up", Key::up},
        {"down", Key::down},
        {"i", Key::info},
        {"other", Key::other},
    }};
    for (const auto &[name, value] : keys)
        if (text == name)
            return value;
    return std::nullopt;
}

std::optional<ScriptAction> action(const std::vector<std::string_view> &parts) {
    const auto name = parts[1];
    const auto count = parts.size() - 2U;
    if (name == "key" && count == 2U) {
        const auto pressed = key(parts[2]);
        if (!pressed)
            return std::nullopt;
        if (parts[3] == "down")
            return ScriptAction{ScriptActionKind::key_down, *pressed, 0};
        if (parts[3] == "up")
            return ScriptAction{ScriptActionKind::key_up, *pressed, 0};
        if (parts[3] == "repeat")
            return ScriptAction{ScriptActionKind::key_repeat, *pressed, 0};
        return std::nullopt;
    }
    if (name == "focus" && count == 1U) {
        if (parts[2] == "on")
            return ScriptAction{ScriptActionKind::focus_gained, Key::other, 0};
        if (parts[2] == "off")
            return ScriptAction{ScriptActionKind::focus_lost, Key::other, 0};
        return std::nullopt;
    }
    if (name == "scroll" && count == 1U) {
        const auto scroll = scroll_key_of_name(parts[2]);
        if (!scroll)
            return std::nullopt;
        ScriptAction scrolled{ScriptActionKind::scroll, Key::other, 0};
        scrolled.scroll = *scroll;
        return scrolled;
    }
    if (name == "video_fail" && count == 1U) {
        const auto failures = number(parts[2]);
        if (!failures || *failures == 0U || *failures > 1000U)
            return std::nullopt;
        return ScriptAction{ScriptActionKind::video_fail, Key::other,
                            static_cast<std::uint32_t>(*failures)};
    }
    if (count != 0U)
        return std::nullopt;
    constexpr std::array<std::pair<std::string_view, ScriptActionKind>, 7> simple{{
        {"minimize", ScriptActionKind::minimize},
        {"restore", ScriptActionKind::restore},
        {"audio_removed", ScriptActionKind::audio_removed},
        {"audio_added", ScriptActionKind::audio_added},
        {"close", ScriptActionKind::close},
        {"corrupt_checkpoints", ScriptActionKind::corrupt_checkpoints},
        {"corrupt_visual_state", ScriptActionKind::corrupt_visual_state},
    }};
    for (const auto &[text, kind] : simple)
        if (name == text)
            return ScriptAction{kind, Key::other, 0};
    return std::nullopt;
}

} // namespace

ScriptParseResult parse_input_script(std::string_view text) {
    std::vector<ScriptStep> steps;
    std::size_t number_of_line = 0;
    while (!text.empty()) {
        ++number_of_line;
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
        if (const auto comment = line.find('#'); comment != std::string_view::npos)
            line = line.substr(0, comment);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const auto parts = words(line);
        if (parts.empty())
            continue;
        const auto when = trigger(parts[0]);
        if (!when)
            return ScriptError{number_of_line, "invalid_trigger"};
        if (parts.size() < 2U)
            return ScriptError{number_of_line, "missing_action"};
        const auto what = action(parts);
        if (!what)
            return ScriptError{number_of_line, "invalid_action"};
        steps.push_back({*when, *what});
    }
    return steps;
}

InputScript::InputScript(std::vector<ScriptStep> steps) : steps_(std::move(steps)) {}

std::vector<ScriptAction> InputScript::due(const ScriptState &state) {
    std::vector<ScriptAction> actions;
    if (!last_fired_ms_)
        last_fired_ms_ = state.now_ms;
    while (next_ < steps_.size()) {
        const auto &step = steps_[next_];
        bool ready = false;
        switch (step.trigger.kind) {
        case ScriptTriggerKind::frame:
            ready = state.position && *state.position >= step.trigger.value;
            break;
        case ScriptTriggerKind::paused:
            ready = state.paused && state.position && *state.position == step.trigger.value;
            break;
        case ScriptTriggerKind::after_ms:
            ready = state.now_ms - *last_fired_ms_ >= static_cast<double>(step.trigger.value);
            break;
        }
        if (!ready)
            break;
        actions.push_back(step.action);
        last_fired_ms_ = state.now_ms;
        ++next_;
    }
    return actions;
}

} // namespace ayther::replay_inspection
