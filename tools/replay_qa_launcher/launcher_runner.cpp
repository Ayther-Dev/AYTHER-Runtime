#include "launcher_runner.h"

#include <utility>

namespace ayther::replay_qa_launcher {

class LauncherRunner::QueueObserver final : public audio_qa::CheckObserver {
  public:
    explicit QueueObserver(LauncherRunner &runner) : runner_(runner) {}
    void on_phase(audio_qa::RequestPhase phase) override { runner_.push(phase); }
    void on_replay_state(const audio_qa::ReplayStateView &state) override { runner_.push(state); }
    void on_take_outcome(const audio_qa::TakeOutcome &outcome) override { runner_.push(outcome); }
    void on_report(std::string_view line) override { runner_.push(std::string{line}); }

  private:
    LauncherRunner &runner_;
};

LauncherRunner::LauncherRunner(RunFunction run) : run_(std::move(run)) {}

LauncherRunner::~LauncherRunner() {
    cancel();
    if (worker_.joinable())
        worker_.join();
}

bool LauncherRunner::start(audio_qa::ReplayRequest request) {
    bool expected = false;
    if (!active_.compare_exchange_strong(expected, true))
        return false;
    if (worker_.joinable())
        worker_.join();
    cancel_ = std::make_unique<audio_qa::CancelToken>();
    worker_ = std::thread{[this, request = std::move(request), token = cancel_.get()] {
        QueueObserver observer{*this};
        audio_qa::RequestOutcome outcome;
        try {
            outcome = run_(request, observer, *token);
        } catch (...) {
            outcome.exit_code = 4;
            outcome.issues.push_back({"", "launcher_run_failed"});
        }
        push(std::move(outcome));
        active_.store(false);
    }};
    return true;
}

void LauncherRunner::cancel() noexcept {
    if (active_.load() && cancel_)
        cancel_->request();
}

void LauncherRunner::push(RunnerEvent event) {
    const std::lock_guard lock{mutex_};
    events_.push_back(std::move(event));
}

std::vector<RunnerEvent> LauncherRunner::drain() {
    const std::lock_guard lock{mutex_};
    return std::exchange(events_, {});
}

} // namespace ayther::replay_qa_launcher
