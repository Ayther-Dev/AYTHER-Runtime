#include "hd_initialization.h"

namespace ayther::audio_qa {
namespace {

[[nodiscard]] HdInitializationOperationRecord
invoke(const HdInitializationOperation operation, void *const context,
       const std::string_view unavailable_code) noexcept {
    if (operation == nullptr) {
        return {true, false, unavailable_code, "required operation is unavailable"};
    }
    const auto result = operation(context);
    return {true, result.succeeded, result.code, result.detail};
}

} // namespace

HdInitializationSelection
select_hd_initialization(const HdStateAvailability availability,
                         const HdInitializationOperations &operations) noexcept {
    HdInitializationSelection selection;
    if (availability == HdStateAvailability::supplied) {
        selection.restore =
            invoke(operations.restore_supplied, operations.context, "hd_restore_unavailable");
        if (selection.restore.succeeded) {
            selection.initialization = HdInitialization::restored;
            selection.restore_result = RestoreResult::succeeded;
            selection.reason = "supplied_hd_state_restored";
            selection.evidence_incomplete = false;
            return selection;
        }
        selection.restore_result = RestoreResult::failed;
    }

    selection.fresh =
        invoke(operations.prepare_fresh, operations.context, "fresh_hd_initialization_unavailable");
    if (!selection.fresh.succeeded) {
        selection.reason = "fresh_hd_initialization_failed";
        return selection;
    }

    selection.initialization = HdInitialization::fresh;
    selection.reason = availability == HdStateAvailability::absent ? "hd_state_absent"
                                                                   : "supplied_hd_state_rejected";
    selection.evidence_incomplete = false;
    return selection;
}

} // namespace ayther::audio_qa
