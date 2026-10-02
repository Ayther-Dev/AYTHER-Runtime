#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace runtime {

struct MusicRf18TraceRecord {
    std::uint64_t generation{};
    std::uint64_t occurrence{};
    std::uint64_t appearance{};
    std::uint64_t visit{};
    std::uint64_t iteration{};
    std::string signature_role;
    std::vector<std::uint64_t> candidates;
    std::uint64_t choice{};
    std::string choice_kind;
    std::uint64_t entry_q{};
    std::string output_authority;
    std::uint64_t voice_count{};
    std::vector<std::string> pause_causes;
    std::uint64_t pending_event{};
    std::uint64_t request_frame{};
    std::uint64_t confirmation_frame{};
    std::uint64_t output_boundary{};
    std::string reason;
    std::string producer;
};

class MusicRf18Trace {
  public:
    [[nodiscard]] bool publish(MusicRf18TraceRecord record) {
        if (record.generation == 0 || record.producer.empty() || !normative_reason(record.reason))
            return false;
        records_.push_back(std::move(record));
        return true;
    }

    [[nodiscard]] constexpr std::uint32_t schema_version() const noexcept { return 1; }
    [[nodiscard]] const std::vector<MusicRf18TraceRecord> &records() const noexcept {
        return records_;
    }
    [[nodiscard]] constexpr std::uint64_t policy_recalculations() const noexcept { return 0; }

  private:
    static bool normative_reason(std::string_view reason) noexcept {
        static constexpr std::array reasons{std::string_view{"recognition_timeout"},
                                            std::string_view{"recognition_limit"},
                                            std::string_view{"membership_unconfirmed"},
                                            std::string_view{"position_unconfirmed"},
                                            std::string_view{"transition_conflict"},
                                            std::string_view{"transition_voice_limit"},
                                            std::string_view{"source_exhausted_before_transition"},
                                            std::string_view{"asset_playback_failed"},
                                            std::string_view{"game_pause_unobservable"},
                                            std::string_view{"analysis_limit"},
                                            std::string_view{"restore_failed_rolled_back"},
                                            std::string_view{"restore_failed_state_unknown"}};
        return std::ranges::find(reasons, reason) != reasons.end() ||
               reason.starts_with("incompatible_");
    }

    std::vector<MusicRf18TraceRecord> records_;
};

} // namespace runtime
