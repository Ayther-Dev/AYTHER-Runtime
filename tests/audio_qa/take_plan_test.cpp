// Spec 002, RF-1.2 and RF-1.4: takes are explicit, ordered and identified by position.
#include "take_plan.h"

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

} // namespace

int main() {
    const std::vector<std::string> paths{"a.ayr", "b.arp", "a.ayr"};
    const auto slots = qa::plan_takes(paths);
    expect(slots == std::vector<qa::TakeSlot>{{0, "a.ayr"}, {1, "b.arp"}, {2, "a.ayr"}},
           "RF-1.4: order and explicit repetitions are kept");
    expect(qa::plan_takes(std::vector<std::string>{}).empty(),
           "RF-1.2: no take is added when none was selected");
    expect(qa::run_id_for_position("run-1", 0) == "run-1" &&
               qa::run_id_for_position("run-1", 1) == "run-1-take-2" &&
               qa::run_id_for_position("run-1", 2) == "run-1-take-3",
           "RF-1.4: each position has its own run id");
    if (failures != 0)
        return 1;
    std::cout << "takes are planned by position\n";
    return 0;
}
