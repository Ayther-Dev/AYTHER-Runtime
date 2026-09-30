#include "process_cessation.h"

#include <utility>

namespace ayther::audio_qa {

ProcessCessation::ProcessCessation(Run last_known_run) noexcept
    : status_{ProcessCessationState::connected, std::move(last_known_run), {}} {}

bool ProcessCessation::observe_run(const Run &run) noexcept {
    if (status_.state != ProcessCessationState::connected ||
        run.run_id != status_.last_known_run.run_id ||
        run.request_id != status_.last_known_run.request_id ||
        run.take_id != status_.last_known_run.take_id) {
        return false;
    }
    status_.last_known_run = run;
    return true;
}

bool ProcessCessation::observe_disconnect() noexcept {
    if (status_.state != ProcessCessationState::connected) {
        return false;
    }
    status_.state = ProcessCessationState::disconnected;
    return true;
}

bool ProcessCessation::request_stop() noexcept {
    if (status_.state != ProcessCessationState::disconnected) {
        return false;
    }
    status_.state = ProcessCessationState::stop_requested;
    return true;
}

bool ProcessCessation::mark_unconfirmed() noexcept {
    if (status_.state != ProcessCessationState::stop_requested) {
        return false;
    }
    status_.state = ProcessCessationState::unconfirmed;
    return true;
}

bool ProcessCessation::confirm_exit(const std::int32_t exit_code) noexcept {
    if (status_.state == ProcessCessationState::confirmed) {
        return false;
    }
    status_.state = ProcessCessationState::confirmed;
    status_.exit_code = exit_code;
    status_.last_known_run.cessation_confirmed = true;
    return true;
}

const ProcessCessationStatus &ProcessCessation::status() const noexcept { return status_; }

} // namespace ayther::audio_qa
