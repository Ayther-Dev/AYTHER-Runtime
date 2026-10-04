#pragma once

#include "check_runner.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace ayther::replay_qa_launcher {

// Spec 002, plan §5.14 (RF-2.1, RF-2.3, RF-2.5): runs one request at a time on a worker
// thread. The observer events are queued for the interface thread, which drains them each
// frame; Cancel requests the CancelToken of the running request.
using RunFunction = std::function<audio_qa::RequestOutcome(
    const audio_qa::ReplayRequest &, audio_qa::CheckObserver &, audio_qa::CancelToken &)>;

using RunnerEvent = std::variant<audio_qa::RequestPhase, audio_qa::ReplayStateView,
                                 audio_qa::TakeOutcome, audio_qa::RequestOutcome, std::string>;

class LauncherRunner final {
  public:
    explicit LauncherRunner(RunFunction run = &audio_qa::run_check);
    ~LauncherRunner();
    LauncherRunner(const LauncherRunner &) = delete;
    LauncherRunner &operator=(const LauncherRunner &) = delete;

    // False while a request is active: a second activation never starts another run.
    bool start(audio_qa::ReplayRequest request);
    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_.load(); }
    [[nodiscard]] std::vector<RunnerEvent> drain();

  private:
    class QueueObserver;

    void push(RunnerEvent event);

    RunFunction run_;
    std::thread worker_;
    std::unique_ptr<audio_qa::CancelToken> cancel_;
    std::atomic<bool> active_{};
    std::mutex mutex_;
    std::vector<RunnerEvent> events_;
};

} // namespace ayther::replay_qa_launcher
