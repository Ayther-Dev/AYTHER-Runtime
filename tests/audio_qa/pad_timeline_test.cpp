// Spec 006 (RF-2.1 to RF-2.4): the pad timeline grammar, the Genesis button map, the
// durations and their rounding with the timing of the core, the bounds and the errors with
// their line, and the compiled inputs read through the same source as a take.
#include "pad_timeline.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::TimelineParseResult parsed(std::string_view text) { return qa::parse_pad_timeline(text); }

bool rejected(std::string_view text, qa::TimelineParseError error, std::size_t line) {
    const auto result = parsed(text);
    return result.error == error && result.line == line;
}

std::vector<std::uint16_t> compiled(std::string_view text, double fps) {
    const auto result = parsed(text);
    if (result.error != qa::TimelineParseError::none)
        return {};
    const auto built = qa::compile_pad_timeline(result.timeline, fps);
    return built.error == qa::TimelineCompileError::none ? built.compiled.buttons
                                                         : std::vector<std::uint16_t>{};
}

// RF-2.1: the Genesis names and the RetroPad bits of Genesis Plus GX.
void genesis_map() {
    const auto buttons = qa::genesis_buttons();
    expect(buttons.size() == 12U, "RF-2.1: twelve Genesis buttons");
    const auto bit = [](std::string_view name) {
        return qa::parse_timeline_buttons(name).value_or(0U);
    };
    expect(bit("a") == 0x0002 && bit("b") == 0x0001 && bit("c") == 0x0100,
           "RF-2.1: A, B and C map to RetroPad Y, B and A");
    expect(bit("x") == 0x0400 && bit("y") == 0x0200 && bit("z") == 0x0800,
           "RF-2.1: X, Y and Z map to RetroPad L, X and R");
    expect(bit("start") == 0x0008 && bit("mode") == 0x0004,
           "RF-2.1: Start and Mode map to RetroPad Start and Select");
    expect(bit("up") == 0x0010 && bit("down") == 0x0020 && bit("left") == 0x0040 &&
               bit("right") == 0x0080,
           "RF-2.1: the d-pad keeps its bits");
    expect(bit("a+b") == 0x0003 && bit("right+c") == 0x0180 && bit("START") == 0x0008,
           "RF-2.1: combinations join with + and names ignore case");
    expect(!qa::parse_timeline_buttons("a+a") && !qa::parse_timeline_buttons("select") &&
               !qa::parse_timeline_buttons("a+") && !qa::parse_timeline_buttons(""),
           "RF-2.1: a repeated, unknown or missing name is rejected");
}

// RF-2.2: every keyword, comments, blank lines and case.
void grammar() {
    const auto result = parsed("# boot and enter the game\n"
                               "START power_on\n"
                               "\n"
                               "wait 5s   # title\n"
                               "press start\n"
                               "Press A 100ms\n"
                               "hold right 0.1s\n"
                               "down b\n"
                               "wait 12f\n"
                               "up b\n"
                               "press a+c 6f\n");
    expect(result.error == qa::TimelineParseError::none, "RF-2.2: the sample timeline parses");
    const auto &timeline = result.timeline;
    expect(timeline.start.kind == qa::TimelineStartKind::power_on && timeline.steps.size() == 8U,
           "RF-2.2: power_on start and eight steps");
    if (timeline.steps.size() != 8U)
        return;
    const auto &steps = timeline.steps;
    expect(steps[0].kind == qa::TimelineStepKind::wait && steps[0].line == 4U &&
               steps[0].duration->unit == qa::TimelineDurationUnit::seconds &&
               steps[0].duration->seconds == 5.0,
           "RF-2.2: wait keeps its seconds and its line");
    expect(steps[1].kind == qa::TimelineStepKind::press && steps[1].buttons == 0x0008 &&
               steps[1].duration->unit == qa::TimelineDurationUnit::frames &&
               steps[1].duration->frames == qa::default_press_frames,
           "RF-2.2: a press without duration lasts the default frames");
    expect(steps[2].buttons == 0x0002 && std::abs(steps[2].duration->seconds - 0.1) < 1e-9,
           "RF-2.2: 100ms is a tenth of a second");
    expect(steps[3].kind == qa::TimelineStepKind::press && steps[3].buttons == 0x0080 &&
               std::abs(steps[3].duration->seconds - 0.1) < 1e-9,
           "RF-2.2: hold is a press with a mandatory duration");
    expect(steps[4].kind == qa::TimelineStepKind::down && steps[4].buttons == 0x0001 &&
               !steps[4].duration,
           "RF-2.2: down has no duration");
    expect(steps[5].duration->unit == qa::TimelineDurationUnit::frames &&
               steps[5].duration->frames == 12U,
           "RF-2.2: 12f is twelve frames");
    expect(steps[6].kind == qa::TimelineStepKind::up && steps[7].buttons == 0x0102 &&
               steps[7].duration->frames == 6U,
           "RF-2.2: up releases and a combination presses both");

    const auto state = parsed("start state ../bookmarks/prueba audio.sav\nwait 1f\n");
    expect(state.error == qa::TimelineParseError::none &&
               state.timeline.start.kind == qa::TimelineStartKind::state &&
               state.timeline.start.state_path == "../bookmarks/prueba audio.sav",
           "RF-3.2: a state start keeps its path, blanks included");
    const auto quoted = parsed("start state \"C:/marks/a #1.sav\"\nwait 1f\n");
    expect(quoted.error == qa::TimelineParseError::none &&
               quoted.timeline.start.state_path == "C:/marks/a #1.sav",
           "RF-3.2: a quoted path loses its quotes and keeps a # without a blank before it");
    const auto implicit = parsed("wait 1f\n");
    expect(implicit.error == qa::TimelineParseError::none &&
               implicit.timeline.start.kind == qa::TimelineStartKind::power_on,
           "RF-3.1: without start the timeline begins at power_on");
}

// RF-2.3: each error names its line.
void errors() {
    using Error = qa::TimelineParseError;
    expect(rejected("wait 1f\njump 2f\n", Error::unknown_keyword, 2U), "unknown keyword, line 2");
    expect(rejected("press select\n", Error::unknown_button, 1U), "unknown button");
    expect(rejected("press a+a\n", Error::unknown_button, 1U), "repeated button");
    expect(rejected("wait 5\n", Error::invalid_duration, 1U), "duration without unit");
    expect(rejected("wait -1s\n", Error::invalid_duration, 1U), "negative duration");
    expect(rejected("wait 0f\n", Error::invalid_duration, 1U), "zero frames");
    expect(rejected("wait 54001f\n", Error::invalid_duration, 1U), "more frames than a take");
    expect(rejected("wait\n", Error::missing_duration, 1U), "wait without duration");
    expect(rejected("hold a\n", Error::missing_duration, 1U), "hold without duration");
    expect(rejected("wait 1s 2s\n", Error::unexpected_argument, 1U), "extra argument");
    expect(rejected("down a 1s\n", Error::unexpected_argument, 1U), "down with a duration");
    expect(rejected("start later\n", Error::invalid_start, 1U), "unknown start");
    expect(rejected("start state\n", Error::invalid_start, 1U), "state without path");
    expect(rejected("wait 1f\nstart power_on\n", Error::start_not_first, 2U), "start after a step");
    expect(rejected("start power_on\nstart power_on\nwait 1f\n", Error::start_repeated, 2U),
           "start twice");
    expect(rejected("down a\ndown a\n", Error::button_already_down, 2U), "down twice");
    expect(rejected("up a\n", Error::button_not_down, 1U), "up without down");
    expect(rejected("", Error::no_steps, 0U), "empty file");
    expect(rejected("# only comments\n\n", Error::no_steps, 0U), "only comments");
    std::string many;
    for (std::size_t index = 0; index <= qa::max_timeline_steps; ++index)
        many += "wait 1f\n";
    expect(rejected(many, Error::too_many_steps, qa::max_timeline_steps + 1U),
           "more steps than admitted");
    const std::string large(qa::max_timeline_bytes + 1U, ' ');
    expect(rejected(large, Error::file_too_large, 0U), "file above 64 KiB");
}

// RF-2.2, RF-4.1: seconds become frames with the timing of the core.
void compilation() {
    const auto ntsc = compiled("wait 2f\npress a 3f\nwait 1f\n", 59.92);
    expect(ntsc == std::vector<std::uint16_t>{0, 0, 2, 2, 2, 0},
           "RF-4.1: frames are literal; a press is held exactly its frames");
    expect(compiled("hold right 0.1s\n", 59.92).size() == 6U &&
               compiled("hold right 0.1s\n", 49.70).size() == 5U,
           "RF-4.1: 0.1 s rounds to 6 frames at 59.92 fps and to 5 at 49.70 fps");
    expect(compiled("wait 1s\n", 60.0).size() == 60U &&
               compiled("wait 500ms\n", 60.0).size() == 30U,
           "RF-4.1: 1 s is 60 frames at 60 fps, 500 ms is 30");
    expect(compiled("press start\n", 60.0).size() == qa::default_press_frames,
           "RF-2.2: the default press lasts default_press_frames");
    const auto held = compiled("down b\nwait 2f\npress a 1f\nup b\nwait 1f\n", 60.0);
    expect(held == std::vector<std::uint16_t>{1, 1, 3, 0},
           "RF-2.2: a held button stays in every frame until up, and adds to a press");
    const auto twice = compiled("press a 1f\npress a 1f\n", 60.0);
    expect(twice == std::vector<std::uint16_t>{2, 2},
           "RF-2.2: two presses without a wait are one continuous hold");

    const auto zero = parsed("wait 1ms\n");
    const auto zero_frames = qa::compile_pad_timeline(zero.timeline, 49.0);
    expect(zero_frames.error == qa::TimelineCompileError::duration_rounds_to_zero &&
               zero_frames.line == 1U,
           "RF-2.2: a step that rounds to zero frames is an error with its line");
    const auto long_timeline = parsed("wait 800s\nwait 200s\n");
    const auto too_long = qa::compile_pad_timeline(long_timeline.timeline, 60.0);
    expect(too_long.error == qa::TimelineCompileError::too_many_frames && too_long.line == 2U,
           "RF-2.4: above 54 000 frames the offending step is named");
    const auto only_holds = parsed("down a\nup a\n");
    expect(qa::compile_pad_timeline(only_holds.timeline, 60.0).error ==
               qa::TimelineCompileError::no_frames,
           "RF-2.4: a timeline without frames is rejected");
    expect(qa::compile_pad_timeline(only_holds.timeline, 0.0).error ==
               qa::TimelineCompileError::invalid_fps,
           "RF-4.1: an invalid timing is an error of the core");
}

// The compiled inputs are read through the same source as the inputs of a take.
void input_source() {
    const auto result = parsed("press a 2f\nwait 1f\npress start+c 1f\n");
    const auto built = qa::compile_pad_timeline(result.timeline, 60.0);
    expect(built.error == qa::TimelineCompileError::none && built.compiled.frames == 4U &&
               built.compiled.input_bytes.size() == 8U,
           "the compiled timeline has one little-endian u16 per frame");
    const auto layout = qa::timeline_input_layout(built.compiled);
    expect(layout.frame_count == 4U && layout.inputs.offset == 0U && layout.inputs.size == 8U &&
               layout.raw_state_bytes == 0U,
           "the layout spans exactly the inputs and declares no state");
    auto source = qa::make_timeline_input_source(built.compiled);
    expect(source.error == qa::RecordingInputSourceError::none && source.source.frame_count() == 4U,
           "the input source accepts the compiled layout");
    std::vector<std::uint16_t> read;
    while (const auto input = source.source.next())
        read.push_back(input->buttons);
    expect(read == built.compiled.buttons && read == std::vector<std::uint16_t>{2, 2, 0, 0x0108},
           "the source yields the compiled masks in order and then is exhausted");
}

void paths() {
    const std::filesystem::path timeline{"C:/qa/scripts/boot.timeline"};
    expect(qa::resolve_timeline_state_path(timeline, "../bookmarks/title.sav") ==
               std::filesystem::path{"C:/qa/bookmarks/title.sav"}.lexically_normal(),
           "RF-3.2: a relative state path resolves against the timeline file");
    expect(qa::resolve_timeline_state_path(timeline, "D:/marks/title.sav") ==
               std::filesystem::path{"D:/marks/title.sav"},
           "RF-3.2: an absolute state path stays");
    expect(qa::timeline_fm_sidecar_path("C:/qa/bookmarks/title.sav") ==
               std::filesystem::path{"C:/qa/bookmarks/title.fm"},
           "RF-3.3: the sidecar is the state with the .fm extension");
}

} // namespace

int main() {
    genesis_map();
    grammar();
    errors();
    compilation();
    input_source();
    paths();
    if (failures != 0)
        return 1;
    std::cout << "pad timelines parse, compile and feed the replay input source\n";
    return 0;
}
