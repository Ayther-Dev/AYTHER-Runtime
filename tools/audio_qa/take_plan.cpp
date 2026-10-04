#include "take_plan.h"

namespace ayther::audio_qa {

std::vector<TakeSlot> plan_takes(std::span<const std::string> paths) {
    std::vector<TakeSlot> slots;
    slots.reserve(paths.size());
    for (std::size_t position = 0; position < paths.size(); ++position)
        slots.push_back({position, paths[position]});
    return slots;
}

std::string run_id_for_position(std::string_view base_run_id, std::size_t position) {
    std::string run_id{base_run_id};
    if (position != 0U)
        run_id += "-take-" + std::to_string(position + 1U);
    return run_id;
}

} // namespace ayther::audio_qa
