// Spec 002, BR-169 (RF-2.1, RF-2.3, RF-2.5): Start runs run_check on a worker thread
// without blocking the interface; a second activation does not create another run; Cancel
// requests the CancelToken. The run function is a fake that waits for its token.
#include "launcher_runner.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>
#include <variant>

namespace la = ayther::replay_qa_launcher;
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
    std::atomic<int> runs{0};
    la::LauncherRunner runner{
        [&runs](const qa::ReplayRequest &, qa::CheckObserver &observer, qa::CancelToken &cancel) {
            ++runs;
            observer.on_phase({qa::RequestPhaseKind::running, 0});
            while (!cancel.requested())
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            qa::RequestOutcome outcome;
            outcome.exit_code = 2;
            outcome.per_take = {qa::NotStartedTake{0, "a.ayr", "not_started_after_cancellation"}};
            observer.on_phase({qa::RequestPhaseKind::closed, 0});
            return outcome;
        }};

    const auto started = std::chrono::steady_clock::now();
    expect(runner.start(qa::ReplayRequest{}), "RF-2.1: Start launches the request");
    expect(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{500},
           "RF-2.3: Start returns without waiting for the request");
    expect(!runner.start(qa::ReplayRequest{}) && runner.active(),
           "RF-2.3: a second activation does not create another run");
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    runner.cancel();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (runner.active() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    expect(!runner.active() && runs == 1, "RF-2.5: Cancel stops the only run");

    bool saw_running = false;
    bool saw_finished = false;
    for (const auto &event : runner.drain()) {
        if (const auto *phase = std::get_if<qa::RequestPhase>(&event))
            saw_running = saw_running || phase->kind == qa::RequestPhaseKind::running;
        if (const auto *finished = std::get_if<qa::RequestOutcome>(&event))
            saw_finished = finished->exit_code == 2;
    }
    expect(saw_running && saw_finished,
           "RF-2.3: the interface thread receives the phases and the outcome");
    expect(runner.start(qa::ReplayRequest{}), "a finished run allows a new one");
    runner.cancel();
    if (failures != 0)
        return 1;
    std::cout << "requests run on a worker thread\n";
    return 0;
}
