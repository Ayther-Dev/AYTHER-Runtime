#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

// Spec 002 (RF-1.2, RF-1.4): a take is identified by its position in the request.
// The same path may appear more than once; each occurrence is its own run.
// There is no default take: the caller must select at least one explicitly.
struct TakeSlot {
    std::size_t position{};
    std::string path;
    bool operator==(const TakeSlot &) const = default;
};

[[nodiscard]] std::vector<TakeSlot> plan_takes(std::span<const std::string> paths);

// `run-x` for the first take, `run-x-take-N` (N = position + 1) for the next ones,
// the naming the reference already used.
[[nodiscard]] std::string run_id_for_position(std::string_view base_run_id, std::size_t position);

} // namespace ayther::audio_qa
