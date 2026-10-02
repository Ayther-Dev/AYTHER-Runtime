#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace runtime {

enum class RestoredMusicNode { intro, loop, link };

struct MusicRestorePositionInput {
    std::uint64_t old_generation{};
    std::uint64_t new_generation{};
    std::uint64_t occurrence{};
    std::uint64_t appearance{};
    std::uint64_t visit{};
    std::uint64_t iteration{};
    RestoredMusicNode node{RestoredMusicNode::intro};
    std::uint64_t source_cursor{};
    std::uint64_t envelope_cursor{};
    std::uint64_t source_rate{};
    std::uint64_t output_rate{};
    bool host_paused{};
};

struct MusicRestoredPosition {
    std::uint64_t generation{};
    std::uint64_t occurrence{};
    std::uint64_t appearance{};
    std::uint64_t visit{};
    std::uint64_t iteration{};
    RestoredMusicNode node{RestoredMusicNode::intro};
    std::uint64_t output_cursor{};
    std::uint64_t envelope_cursor{};
    std::uint64_t rational_numerator{};
    std::uint64_t rational_denominator{};
    std::uint64_t conversion_error_samples{};
    bool host_paused{};
    bool new_visit{};
    bool new_trigger{};
};

class MusicRestorePositionResolver {
  public:
    [[nodiscard]] std::optional<MusicRestoredPosition>
    restore(const MusicRestorePositionInput &input) const noexcept {
        if (input.new_generation <= input.old_generation || input.occurrence == 0 ||
            input.appearance == 0 || input.source_rate == 0 || input.output_rate == 0 ||
            input.source_cursor > std::numeric_limits<std::uint64_t>::max() / input.output_rate)
            return std::nullopt;
        const auto numerator = input.source_cursor * input.output_rate;
        const auto quotient = numerator / input.source_rate;
        const auto remainder = numerator % input.source_rate;
        const auto rounded = quotient + (remainder >= (input.source_rate + 1) / 2 ? 1 : 0);
        return MusicRestoredPosition{input.new_generation,
                                     input.occurrence,
                                     input.appearance,
                                     input.visit,
                                     input.iteration,
                                     input.node,
                                     rounded,
                                     input.envelope_cursor,
                                     numerator,
                                     input.source_rate,
                                     remainder == 0 ? 0ULL : 1ULL,
                                     input.host_paused,
                                     false,
                                     false};
    }
};

} // namespace runtime
