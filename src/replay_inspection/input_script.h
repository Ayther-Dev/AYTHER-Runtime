#pragma once

#include "debug_view.h"
#include "key_router.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, BR-135 and BR-153 (RF-2.6, RF-4, RF-5, RF-6): the scripted input of the QA tests,
// which drive the inspection without a person. One step per line, `#` starts a comment:
//
//   <trigger> <action>
//   trigger: frame=<k>    the take completed frame k or a later one
//            paused=<k>   the take is paused at frame k
//            after=<ms>   ms milliseconds after the step before (or the start)
//   action:  key <space|left|right|up|down|i|other> <down|up|repeat>
//            focus <on|off> | minimize | restore | video_fail <n> | audio_removed |
//            audio_added | close | corrupt_checkpoints | corrupt_visual_state |
//            scroll <wheel_up|wheel_down|page_up|page_down|home|end>
//
// Steps fire strictly in order. The Runtime reads a script only in a QA session and only from
// AYTHER_QA_INPUT_SCRIPT; without presentation the actions reach the inspection directly, and
// with a window they are delivered as SDL events.
enum class ScriptTriggerKind { frame, paused, after_ms };

struct ScriptTrigger {
    ScriptTriggerKind kind{ScriptTriggerKind::after_ms};
    std::uint64_t value{};
};

enum class ScriptActionKind {
    key_down,
    key_up,
    key_repeat,
    focus_lost,
    focus_gained,
    minimize,
    restore,
    video_fail,
    audio_removed,
    audio_added,
    close,
    // BR-143: damages the stored checkpoints so that the next recovery fails: the core state
    // (rejected before touching the session) or the visual state (rejected after the core).
    corrupt_checkpoints,
    corrupt_visual_state,
    // BR-145: scrolls the debug overlay.
    scroll,
};

struct ScriptAction {
    ScriptActionKind kind{ScriptActionKind::close};
    Key key{Key::other};
    // video_fail: the number of presentations that fail.
    std::uint32_t count{};
    ScrollKey scroll{ScrollKey::home};
};

struct ScriptStep {
    ScriptTrigger trigger;
    ScriptAction action;
};

struct ScriptError {
    // From 1.
    std::size_t line{};
    std::string reason;
};

using ScriptParseResult = std::variant<std::vector<ScriptStep>, ScriptError>;

[[nodiscard]] ScriptParseResult parse_input_script(std::string_view text);

// What the script sees of the take when it is evaluated.
struct ScriptState {
    // The last completed or presented frame; absent before the first one.
    std::optional<std::uint32_t> position;
    bool paused{};
    double now_ms{};
};

class InputScript final {
  public:
    explicit InputScript(std::vector<ScriptStep> steps);

    // The actions due now, in order; a fired step lets the next one be evaluated at once.
    [[nodiscard]] std::vector<ScriptAction> due(const ScriptState &state);
    [[nodiscard]] bool finished() const noexcept { return next_ >= steps_.size(); }

  private:
    std::vector<ScriptStep> steps_;
    std::size_t next_{};
    std::optional<double> last_fired_ms_;
};

} // namespace ayther::replay_inspection
