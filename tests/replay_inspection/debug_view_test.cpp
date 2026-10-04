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
    scroll();
    if (failures != 0)
        return 1;
    std::cout << "the debug overlay draws the record and scrolls\n";
    return 0;
}
