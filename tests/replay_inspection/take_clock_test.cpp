// Spec 002, BR-119 (RF-3.1, RF-7.1, RF-7.10; plan §5.8): the only conversion between
// take frames and times. Before the first frame there is no current frame; frame k has
// elapsed (k+1)·T; at N−1 nothing remains; the Engine counts frames from one.
#include "take_clock.h"

#include <cmath>
#include <iostream>
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

bool near(double left, double right) { return std::abs(left - right) < 1e-9; }

} // namespace

int main() {
    const ri::TakeClock clock{6, 60.0};
    const auto before = clock.before_first();
    expect(!before.frame && near(before.elapsed_ms, 0.0) && near(before.remaining_ms, 100.0),
           "RF-3.1: before the first frame there is no current frame and nothing elapsed");
    const auto first = clock.at(0);
    expect(first.frame == 0U && near(first.elapsed_ms, 1000.0 / 60.0) &&
               near(first.remaining_ms, 100.0 - 1000.0 / 60.0),
           "RF-7.1: frame zero has elapsed one period");
    const auto middle = clock.at(2);
    expect(near(middle.elapsed_ms, 50.0) && near(middle.remaining_ms, 50.0),
           "RF-7.10: frame k has elapsed (k+1)·T");
    const auto last = clock.at(5);
    expect(last.frame == 5U && near(last.elapsed_ms, 100.0) && near(last.remaining_ms, 0.0),
           "RF-7.10: at N−1 nothing remains");
    expect(!clock.at(6).frame, "a frame outside the take has no time");

    const ri::TakeClock single{1, 59.922743};
    expect(near(single.before_first().remaining_ms, 1000.0 / 59.922743) &&
               single.at(0).frame == 0U && near(single.at(0).remaining_ms, 0.0),
           "RF-2.8: a one-frame take starts and ends on frame zero");

    expect(!clock.take_frame_from_engine(0) && clock.take_frame_from_engine(1) == 0U &&
               clock.take_frame_from_engine(6) == 5U && !clock.take_frame_from_engine(7),
           "plan §5.8: the Engine frame index is one more than the take frame");
    expect(ri::TakeClock{0, 60.0}.frames() == 0U && !ri::TakeClock{0, 60.0}.at(0).frame,
           "an empty take has no frames");
    if (failures != 0)
        return 1;
    std::cout << "take frames and times are converted in one place\n";
    return 0;
}
