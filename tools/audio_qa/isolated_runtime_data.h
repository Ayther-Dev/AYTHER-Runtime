#pragma once

#include "isolated_saves.h"

#include <filesystem>
#include <variant>

namespace ayther::audio_qa {

class IsolatedRuntimeData final {
  public:
    explicit IsolatedRuntimeData(IsolatedSaves isolation);
    IsolatedRuntimeData(const IsolatedRuntimeData &) = delete;
    IsolatedRuntimeData &operator=(const IsolatedRuntimeData &) = delete;
    IsolatedRuntimeData(IsolatedRuntimeData &&) noexcept = default;
    IsolatedRuntimeData &operator=(IsolatedRuntimeData &&) noexcept = default;

    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    [[nodiscard]] const SaveTreeSnapshot &user_snapshot() const noexcept;
    [[nodiscard]] SaveTreeVerificationResult verify_user_data_unchanged() const noexcept;

  private:
    IsolatedSaves isolation_;
};

using IsolatedRuntimeDataResult = std::variant<IsolatedRuntimeData, IsolatedSavesError>;

[[nodiscard]] IsolatedRuntimeDataResult
prepare_isolated_runtime_data(const std::filesystem::path &user_data_directory,
                              const std::filesystem::path &private_directory) noexcept;

} // namespace ayther::audio_qa
