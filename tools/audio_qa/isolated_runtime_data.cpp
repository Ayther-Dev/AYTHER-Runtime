#include "isolated_runtime_data.h"

#include <utility>

namespace ayther::audio_qa {

IsolatedRuntimeData::IsolatedRuntimeData(IsolatedSaves isolation)
    : isolation_(std::move(isolation)) {}

const std::filesystem::path &IsolatedRuntimeData::directory() const noexcept {
    return isolation_.directory();
}

const SaveTreeSnapshot &IsolatedRuntimeData::user_snapshot() const noexcept {
    return isolation_.user_snapshot();
}

SaveTreeVerificationResult IsolatedRuntimeData::verify_user_data_unchanged() const noexcept {
    return isolation_.verify_user_saves_unchanged();
}

IsolatedRuntimeDataResult
prepare_isolated_runtime_data(const std::filesystem::path &user_data_directory,
                              const std::filesystem::path &private_directory) noexcept {
    auto isolation = prepare_isolated_saves(user_data_directory, private_directory);
    if (const auto *error = std::get_if<IsolatedSavesError>(&isolation)) {
        return *error;
    }
    return IsolatedRuntimeData{std::move(std::get<IsolatedSaves>(isolation))};
}

} // namespace ayther::audio_qa
