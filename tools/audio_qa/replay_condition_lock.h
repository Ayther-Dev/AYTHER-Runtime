#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ayther::audio_qa {

struct ReplayConditions {
    std::string profile_id;
    std::uint32_t muted_audio_buses{};
    std::uint32_t speed_numerator{1};
    std::uint32_t speed_denominator{1};
    bool operator==(const ReplayConditions &) const = default;
};

enum class ReplayConditionChangeSource {
    configuration,
    external_reload,
};

enum ReplayConditionField : std::uint32_t {
    condition_none = 0,
    condition_profile = 1U << 0U,
    condition_mute = 1U << 1U,
    condition_speed = 1U << 2U,
};

enum class ReplayConditionDisposition {
    unchanged,
    rejected,
};

struct ReplayConditionChangeResult {
    ReplayConditionDisposition disposition{ReplayConditionDisposition::unchanged};
    ReplayConditionChangeSource source{ReplayConditionChangeSource::configuration};
    std::uint32_t differing_fields{};
    std::string_view code;
};

class ReplayConditionLock final {
  public:
    explicit ReplayConditionLock(ReplayConditions conditions) noexcept;

    [[nodiscard]] const ReplayConditions &conditions() const noexcept;
    [[nodiscard]] ReplayConditionChangeResult
    check_change(const ReplayConditions &proposed,
                 ReplayConditionChangeSource source) const noexcept;

  private:
    ReplayConditions conditions_;
};

} // namespace ayther::audio_qa
