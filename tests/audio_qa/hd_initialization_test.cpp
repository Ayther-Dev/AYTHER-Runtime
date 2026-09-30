#include "hd_initialization.h"

#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Operations {
    qa::HdInitializationOperationResult restore{true, "restored", {}};
    qa::HdInitializationOperationResult fresh{true, "fresh", {}};
    unsigned restore_calls{};
    unsigned fresh_calls{};
};

qa::HdInitializationOperationResult restore(void *const value) noexcept {
    auto &operations = *static_cast<Operations *>(value);
    ++operations.restore_calls;
    return operations.restore;
}

qa::HdInitializationOperationResult fresh(void *const value) noexcept {
    auto &operations = *static_cast<Operations *>(value);
    ++operations.fresh_calls;
    return operations.fresh;
}

qa::HdInitializationSelection select(const qa::HdStateAvailability availability,
                                     Operations &operations) noexcept {
    return qa::select_hd_initialization(availability, {&operations, restore, fresh});
}

} // namespace

int main() {
    try {
        Operations operations;
        auto selection = select(qa::HdStateAvailability::absent, operations);
        require(selection.initialization == qa::HdInitialization::fresh &&
                    selection.restore_result == qa::RestoreResult::not_attempted &&
                    selection.reason == "hd_state_absent" && !selection.restore.attempted &&
                    selection.fresh.attempted && selection.fresh.succeeded &&
                    !selection.evidence_incomplete && operations.restore_calls == 0 &&
                    operations.fresh_calls == 1,
                "absent_hd_history_did_not_select_verified_fresh");

        operations = {};
        selection = select(qa::HdStateAvailability::supplied, operations);
        require(selection.initialization == qa::HdInitialization::restored &&
                    selection.restore_result == qa::RestoreResult::succeeded &&
                    selection.reason == "supplied_hd_state_restored" &&
                    selection.restore.attempted && selection.restore.succeeded &&
                    !selection.fresh.attempted && !selection.evidence_incomplete &&
                    operations.restore_calls == 1 && operations.fresh_calls == 0,
                "compatible_supplied_hd_state_not_selected");

        operations = {};
        operations.restore = {false, "frame_mismatch", "header frame"};
        selection = select(qa::HdStateAvailability::supplied, operations);
        require(selection.initialization == qa::HdInitialization::fresh &&
                    selection.restore_result == qa::RestoreResult::failed &&
                    selection.reason == "supplied_hd_state_rejected" &&
                    selection.restore.code == "frame_mismatch" &&
                    selection.restore.detail == "header frame" && selection.fresh.succeeded &&
                    !selection.evidence_incomplete && operations.restore_calls == 1 &&
                    operations.fresh_calls == 1,
                "rejected_supplied_state_did_not_fall_back_to_fresh");

        operations = {};
        operations.restore = {false, "invalid_voice", "voices"};
        operations.fresh = {false, "fresh_failed", "backend"};
        selection = select(qa::HdStateAvailability::supplied, operations);
        require(selection.initialization == qa::HdInitialization::unknown &&
                    selection.restore_result == qa::RestoreResult::failed &&
                    selection.reason == "fresh_hd_initialization_failed" &&
                    selection.restore.code == "invalid_voice" &&
                    selection.fresh.code == "fresh_failed" && selection.evidence_incomplete &&
                    operations.restore_calls == 1 && operations.fresh_calls == 1,
                "unverified_fallback_was_reported_as_usable");

        std::puts("hd_initialization_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "hd_initialization_test: %s\n", error.what());
        return 1;
    }
}
