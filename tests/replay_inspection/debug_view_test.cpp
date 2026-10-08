// Spec 002, BR-145 (RF-7.1, RF-7.2, RF-7.3, RNF-4; plan §5.5, D15): what the debug overlay
// draws from a FrameRecord, and how it scrolls. Values carry their units in the label and are
// never cut; the overlay scrolls with the wheel and with Page Up, Page Down, Home and End, and
// has no input for Space, the arrows or I, which always belong to the inspection.
#include "debug_view.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

ri::FrameRecord record() {
    ri::FrameRecord record;
    record.frame = 4309;
    record.visit = 2;
    record.general = {"C:/roms/Golden Axe (World) (Rev A).md", "C:/takes/Toma 3.ayr", std::nullopt,
                      "paused", 7892};
    record.measurements = {3.25, std::nullopt};
    ri::OccurrenceRow replaced;
    replaced.index = 0;
    replaced.slot = 12;
    replaced.chain = 3;
    replaced.identity = 0xABCDEF0123456789ULL;
    replaced.status = ri::render::OccurrenceStatus::replaced;
    replaced.pose = "chicken_leg_walk_3_with_a_rather_long_pose_key_that_must_not_be_cut";
    replaced.asset = "sprites/chicken_leg/walk_3.png";
    replaced.draw = ri::render::DrawOutcome::partitioned;
    ri::OccurrenceRow waiting;
    waiting.index = 1;
    waiting.slot = 13;
    waiting.chain = 4;
    waiting.status = ri::render::OccurrenceStatus::assigned_not_applied;
    waiting.reason = "texture_pending";
    record.rows = {replaced, waiting};
    record.overflow = ri::RowOverflow{300, 256};
    return record;
}

bool has(const std::vector<ri::DebugLine> &lines, std::string_view text) {
    return std::any_of(lines.begin(), lines.end(), [text](const ri::DebugLine &line) {
        return (line.label + " " + line.value).find(text) != std::string::npos;
    });
}

bool has_label(const std::vector<ri::DebugLine> &lines, std::string_view label) {
    return std::any_of(lines.begin(), lines.end(),
                       [label](const ri::DebugLine &line) { return line.label == label; });
}

std::size_t occurrence_lines(const std::vector<ri::DebugLine> &lines) {
    return static_cast<std::size_t>(
        std::count_if(lines.begin(), lines.end(), [](const ri::DebugLine &line) {
            return !line.label.empty() && line.label.front() == '#';
        }));
}

ri::FrameRecord limit_record(std::size_t total) {
    auto value = record();
    value.rows.resize(std::min(total, ri::max_record_rows));
    if (total > ri::max_record_rows)
        value.overflow = ri::RowOverflow{total, ri::max_record_rows};
    else
        value.overflow.reset();
    return value;
}

void lines() {
    const ri::TakeClock clock{7892, 60.0};
    const auto spanish = ri::debug_lines(record(), clock, ri::ReplayLanguage::spanish);
    expect(has(spanish, "Sin pack") && has(spanish, "Golden Axe (World) (Rev A).md") &&
               has(spanish, "Toma 3.ayr"),
           "RF-7.1: the general data, with «Sin pack»");
    expect(has(spanish, "Frame 4309") && has(spanish, "Frames totales 7892") &&
               has(spanish, "Transcurrido (ms) 71833.33") &&
               has(spanish, "Restante (ms) 59700.00") && has(spanish, "Procesamiento (ms) 3.25"),
           "RF-7.1, RF-7.4: frame, times and measures with their units");
    expect(!has(spanish, "FPS"), "RF-7.7: an unknown measure is not drawn");
    expect(
        has(spanish, "slot 12") && has(spanish, "chain 3") && has(spanish, "abcdef0123456789") &&
            has(spanish, "replaced") &&
            has(spanish, "chicken_leg_walk_3_with_a_rather_long_pose_key_that_must_not_be_cut") &&
            has(spanish, "sprites/chicken_leg/walk_3.png") && has(spanish, "partitioned"),
        "RF-7.2, RF-7.3: each occurrence with its identity, pose, asset and draw, uncut");
    expect(has(spanish, "assigned_not_applied") && has(spanish, "texture_pending"),
           "RF-7.9: the known reason of an assignment not applied");
    expect(has(spanish, "Filas no mostradas 44 (300 > 256)"),
           "RNF-3: the rows over the limit are announced, never cut silently");
    const auto english = ri::debug_lines(record(), clock, ri::ReplayLanguage::english);
    expect(has(english, "No pack") && has(english, "Processing (ms) 3.25"),
           "RNF-7: the overlay speaks the language of the request");
}

void row_limits() {
    const ri::TakeClock clock{7892, 60.0};
    const auto exact =
        ri::debug_lines(limit_record(ri::max_record_rows), clock, ri::ReplayLanguage::spanish);
    expect(occurrence_lines(exact) == ri::max_record_rows && has(exact, "Ocurrencias 256") &&
               !has_label(exact, "Filas no mostradas"),
           "RNF-3/P-11: 256 consultable rows are all shown without overflow");

    const auto one_over =
        ri::debug_lines(limit_record(ri::max_record_rows + 1U), clock, ri::ReplayLanguage::spanish);
    expect(occurrence_lines(one_over) == ri::max_record_rows && has(one_over, "Ocurrencias 256") &&
               has(one_over, "Filas no mostradas 1 (257 > 256)"),
           "RNF-3/P-11: the overlay reports one hidden row and total 257");

    const auto crowded = ri::debug_lines(limit_record(300U), clock, ri::ReplayLanguage::english);
    expect(occurrence_lines(crowded) == ri::max_record_rows && has(crowded, "Occurrences 256") &&
               has(crowded, "Rows not shown 44 (300 > 256)"),
           "RNF-3/P-11: the overlay reports 44 hidden rows and total 300");
}

void scroll() {
    ri::DebugScroll scroll;
    scroll.apply(ri::ScrollKey::page_down, 10, 35);
    expect(scroll.first() == 10U, "Page Down moves one page");
    scroll.apply(ri::ScrollKey::wheel_down, 10, 35);
    expect(scroll.first() == 13U, "the wheel moves three lines");
    scroll.apply(ri::ScrollKey::end, 10, 35);
    expect(scroll.first() == 25U, "End shows the last page");
    scroll.apply(ri::ScrollKey::page_down, 10, 35);
    expect(scroll.first() == 25U, "the scroll stops at the end");
    scroll.apply(ri::ScrollKey::page_up, 10, 35);
    scroll.apply(ri::ScrollKey::wheel_up, 10, 35);
    expect(scroll.first() == 12U, "Page Up and the wheel go back");
    scroll.apply(ri::ScrollKey::home, 10, 35);
    expect(scroll.first() == 0U, "Home goes to the first line");
    scroll.apply(ri::ScrollKey::end, 10, 5);
    expect(scroll.first() == 0U, "a short record does not scroll");
    expect(!ri::scroll_key_of_name("space") && !ri::scroll_key_of_name("left") &&
               !ri::scroll_key_of_name("right") && !ri::scroll_key_of_name("i") &&
               ri::scroll_key_of_name("page_down") == ri::ScrollKey::page_down,
           "D15: Space, the arrows and I are never scroll keys");
}

} // namespace

int main() {
    lines();
    row_limits();
    scroll();
    if (failures != 0)
        return 1;
    std::cout << "the debug overlay draws the record and scrolls\n";
    return 0;
}
