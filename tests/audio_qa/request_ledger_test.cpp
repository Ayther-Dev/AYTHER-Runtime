#include "request_ledger.h"

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <variant>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

qa::Request request(const char *const request_id = "request-143") {
    return {request_id,
            "session-143",
            "conditions-a",
            {"take-main", "take-extra"},
            qa::Admission::pending};
}

qa::Run run(const char *const request_id = "request-143", const char *const run_id = "run-143") {
    qa::Run value;
    value.run_id = run_id;
    value.request_id = request_id;
    value.take_id = "take-main";
    return value;
}

qa::RequestLedger take_ledger(qa::RequestLedgerOpenResult result) {
    auto *ledger = std::get_if<qa::RequestLedger>(&result);
    require(ledger != nullptr, "request_ledger_open_failed");
    return std::move(*ledger);
}

const qa::RequestRegistrationResult &
registration(const qa::RequestLedgerRegistrationResult &result) {
    const auto *value = std::get_if<qa::RequestRegistrationResult>(&result);
    require(value != nullptr, "request_ledger_registration_failed");
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-143-ledger";
    remove_tree(fixture);
    try {
        require(std::filesystem::create_directory(fixture), "request_ledger_fixture_failed");
        {
            auto ledger = take_ledger(qa::open_request_ledger(fixture));
            require(ledger.size() == 0 && ledger.generation() == 0,
                    "new_request_ledger_was_not_empty");
            const auto accepted = ledger.register_request(request(), run());
            require(registration(accepted).decision == qa::RequestRegistrationDecision::accepted &&
                        ledger.size() == 1 && ledger.generation() == 1,
                    "first_request_was_not_persisted");
            auto playing = registration(accepted).run;
            playing.phase = qa::Phase::playing;
            playing.playback_result = qa::PlaybackResult::in_progress;
            const auto updated = ledger.update_run(playing);
            require(std::get_if<bool>(&updated) != nullptr && std::get<bool>(updated) &&
                        ledger.generation() == 2,
                    "request_run_state_was_not_persisted");
        }

        auto reopened = take_ledger(qa::open_request_ledger(fixture));
        std::size_t launches{};
        const auto resent = reopened.register_request(request(), run());
        if (registration(resent).decision == qa::RequestRegistrationDecision::accepted) {
            ++launches;
        }
        require(registration(resent).decision == qa::RequestRegistrationDecision::known &&
                    registration(resent).run.phase == qa::Phase::playing &&
                    registration(resent).run.playback_result == qa::PlaybackResult::in_progress &&
                    launches == 0 && reopened.size() == 1 && reopened.generation() == 2,
                "reopened_request_would_have_launched_again");

        auto conflict = request();
        conflict.conditions_id = "conditions-b";
        require(registration(reopened.register_request(conflict, run())).decision ==
                    qa::RequestRegistrationDecision::identity_conflict,
                "reopened_identity_conflict_was_not_preserved");

#ifdef _WIN32
        const auto lock =
            CreateFileW(reopened.path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "request_ledger_lock_failed");
        const auto interrupted =
            reopened.register_request(request("request-143-b"), run("request-143-b", "run-143-b"));
        const bool publication_failed =
            std::get_if<qa::RequestLedgerError>(&interrupted) != nullptr &&
            std::get<qa::RequestLedgerError>(interrupted) == qa::RequestLedgerError::publish_failed;
        require(CloseHandle(lock) != 0, "request_ledger_unlock_failed");
        require(publication_failed && reopened.size() == 1 && reopened.generation() == 2,
                "failed_ledger_publication_changed_memory_state");
        auto after_failure = take_ledger(qa::open_request_ledger(fixture));
        require(after_failure.size() == 1 && after_failure.generation() == 2 &&
                    registration(after_failure.register_request(request(), run())).decision ==
                        qa::RequestRegistrationDecision::known,
                "failed_ledger_publication_changed_durable_state");
#endif

        remove_tree(fixture);
        std::puts("request_ledger_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "request_ledger_test: %s\n", error.what());
        return 1;
    }
}
