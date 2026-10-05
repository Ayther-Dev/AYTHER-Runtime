// Spec 002, BR-074 (RF-5.8, RF-2.13; plan §4.6, contracts.md C2): the traversal of a take
// is recorded compactly as linear segments plus explicit visits, with repetitions, and is
// written durably at every confirmation point.
#include "inspection_evidence.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
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

qa::InspectionEvent event(std::uint64_t seq, std::string control, std::uint64_t before,
                          std::uint64_t after) {
    return {seq, std::move(control), before, after, seq, 1000U * seq};
}

// The example of plan §4.6: play to 4310, pause, step back, forward, back, resume.
qa::TraversalRecorder plan_example() {
    qa::TraversalRecorder recorder{7892};
    for (std::uint64_t frame = 0; frame <= 4310; ++frame)
        recorder.frame_played(frame);
    recorder.inspection(event(1, "pause", 4310, 4310));
    recorder.inspection(event(2, "step_back", 4310, 4309));
    recorder.inspection(event(3, "step_forward", 4309, 4310));
    recorder.inspection(event(4, "step_back", 4310, 4309));
    recorder.inspection(event(5, "resume", 4309, 4309));
    for (std::uint64_t frame = 4310; frame <= 7891; ++frame)
        recorder.frame_played(frame);
    return recorder;
}

void segments_and_visits() {
    const auto document = plan_example().document();
    expect(document.kind == qa::TraversalKind::inspection && !document.linear_completed &&
               document.frames_total == 7892U,
           "RF-5.8: navigation turns the traversal into an inspection");
    expect(document.segments == std::vector<qa::TraversalSegment>{{0, 4310, "linear"}},
           "RF-5.8: the linear part is one compact segment");
    expect(document.visits == std::vector<qa::TraversalVisit>{{1, 4310, "pause"},
                                                              {2, 4309, "step_back"},
                                                              {3, 4310, "step_forward"},
                                                              {4, 4309, "step_back"},
                                                              {5, 4309, "resume"}},
           "RF-5.8: every visit is kept in order, with its repetitions");
    expect(document.resume_after == std::vector<qa::TraversalResume>{{5, 4310, 7891, "continued"}},
           "RF-5.8: playback after the inspection is a continued segment");
}

// The supervisor rebuilds the same document from the terminal and the events alone.
void rebuilt_from_terminal() {
    const std::vector<qa::InspectionEvent> events{
        event(1, "pause", 4310, 4310), event(2, "step_back", 4310, 4309),
        event(3, "step_forward", 4309, 4310), event(4, "step_back", 4310, 4309),
        event(5, "resume", 4309, 4309)};
    expect(qa::traversal_of_take(7892, 7892, events) == plan_example().document(),
           "RF-5.8: the terminal and the events rebuild the traversal");
    const auto linear = qa::traversal_of_take(6, 6, {});
    expect(linear.kind == qa::TraversalKind::linear && linear.linear_completed &&
               linear.segments == std::vector<qa::TraversalSegment>{{0, 5, "linear"}},
           "RF-3.4: a take without navigation is one complete linear segment");
    const auto cancelled = qa::traversal_of_take(600, 40, {});
    expect(!cancelled.linear_completed &&
               cancelled.segments == std::vector<qa::TraversalSegment>{{0, 39, "linear"}},
           "RF-2.13: a cancelled take is not a complete traversal");
}

// D-5 (campaign 2026-10-04, BR-183): every stretch played between two events is kept, not
// only the one after the last event. Pause at 0, resume, pause at 1000, resume, natural end.
void every_resumed_stretch_is_kept() {
    const std::vector<qa::InspectionEvent> events{
        event(1, "overlay_toggle", 0, 0), event(2, "pause", 0, 0), event(3, "resume", 0, 0),
        event(4, "pause", 1000, 1000), event(5, "resume", 1000, 1000)};
    const auto document = qa::traversal_of_take(7892, 7892, events);
    expect(document.segments == std::vector<qa::TraversalSegment>{{0, 0, "linear"}},
           "C2: the linear segment ends at the first event");
    expect(document.resume_after == std::vector<qa::TraversalResume>{{3, 1, 1000, "continued"},
                                                                     {5, 1001, 7891, "continued"}},
           "C2, RF-2.12: frames 1 to 1000, played after the resume seq 3, are recorded");
    expect(document.kind == qa::TraversalKind::linear && document.linear_completed,
           "RF-5.8: pauses without navigation keep the traversal linear and complete");

    // BR-184: pause at 1, ← to 0, → to 1, 2 and 3, resume at 3, and again at 1500.
    const std::vector<qa::InspectionEvent> arrows{
        event(1, "pause", 1, 1),        event(2, "step_back", 1, 0),
        event(3, "step_forward", 0, 1), event(4, "step_forward", 1, 2),
        event(5, "step_forward", 2, 3), event(6, "resume", 3, 3),
        event(7, "pause", 1500, 1500),  event(8, "step_back", 1500, 1499),
        event(9, "resume", 1499, 1499)};
    const auto inspected = qa::traversal_of_take(7892, 7892, arrows);
    expect(inspected.kind == qa::TraversalKind::inspection &&
               inspected.segments == std::vector<qa::TraversalSegment>{{0, 1, "linear"}} &&
               inspected.resume_after ==
                   std::vector<qa::TraversalResume>{{6, 4, 1500, "continued"},
                                                    {9, 1500, 7891, "continued"}},
           "C2: frames 4 to 1500 between the two inspections are recorded");

    // A cancellation while playing after a resume ends the stretch at the last frame consumed.
    const auto cancelled =
        qa::traversal_of_take(7892, 1201,
                              std::vector<qa::InspectionEvent>{event(1, "pause", 500, 500),
                                                               event(2, "resume", 500, 500)});
    expect(cancelled.resume_after == std::vector<qa::TraversalResume>{{2, 501, 1200, "continued"}},
           "C2: the last stretch ends at the last frame consumed");
}

void pause_alone_stays_linear() {
    qa::TraversalRecorder recorder{6};
    for (std::uint64_t frame = 0; frame <= 2; ++frame)
        recorder.frame_played(frame);
    recorder.inspection(event(1, "pause", 2, 2));
    recorder.inspection(event(2, "resume", 2, 2));
    for (std::uint64_t frame = 3; frame <= 5; ++frame)
        recorder.frame_played(frame);
    recorder.inspection(event(3, "interrupted", 5, 5));
    const auto document = recorder.document();
    expect(document.kind == qa::TraversalKind::linear && document.linear_completed,
           "RF-5.8: a pause without navigation keeps the traversal linear and complete");
    expect(document.interruptions == std::vector<qa::TraversalInterruption>{{3, 5}},
           "RF-2.6: an interruption is recorded with its frame");
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void durable_at_each_confirmation() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-traversal";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto path = root / "runs" / "run-1" / "traversal.toml";
    qa::TraversalRecorder recorder{7892};
    for (std::uint64_t frame = 0; frame <= 4310; ++frame)
        recorder.frame_played(frame);
    recorder.inspection(event(1, "pause", 4310, 4310));
    expect(std::holds_alternative<qa::DurablePublishedFile>(
               qa::write_traversal(path, recorder.document())),
           "RNF-5: the traversal is written durably");
    const auto first = qa::read_traversal(path);
    expect(std::holds_alternative<qa::TraversalDocument>(first) &&
               std::get<qa::TraversalDocument>(first) == recorder.document(),
           "RF-2.13: the confirmed traversal reads back unchanged");
    recorder.inspection(event(2, "step_back", 4310, 4309));
    expect(std::holds_alternative<qa::DurablePublishedFile>(
               qa::write_traversal(path, recorder.document())),
           "RNF-5: each confirmation point rewrites it");
    const auto second = qa::read_traversal(path);
    expect(std::holds_alternative<qa::TraversalDocument>(second) &&
               std::get<qa::TraversalDocument>(second).visits.size() == 2U &&
               std::get<qa::TraversalDocument>(second).kind == qa::TraversalKind::inspection,
           "RF-5.8: the rewritten traversal has the new visit");
    const auto text = read_text(path);
    expect(text.find("schema_version = 1") != std::string::npos &&
               text.find("schema_minor = 1") != std::string::npos,
           "C2: traversal.toml is schema 1.1");
    std::string future = text;
    future.replace(future.find("schema_version = 1"), 18, "schema_version = 2");
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << future;
    }
    expect(std::holds_alternative<qa::TraversalReadError>(qa::read_traversal(path)) &&
               read_text(path) == future,
           "RNF-5: an unknown major is rejected without modifying it");
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main() {
    segments_and_visits();
    rebuilt_from_terminal();
    every_resumed_stretch_is_kept();
    pause_alone_stays_linear();
    durable_at_each_confirmation();
    if (failures != 0)
        return 1;
    std::cout << "traversals are recorded as segments and visits\n";
    return 0;
}
