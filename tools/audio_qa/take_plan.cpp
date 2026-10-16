#include "take_plan.h"

namespace ayther::audio_qa {

std::vector<TakeSlot> plan_takes(std::span<const std::string> paths) {
    std::vector<TakeSlot> slots;
    slots.reserve(paths.size());
    for (std::size_t position = 0; position < paths.size(); ++position)
        slots.push_back({position, paths[position]});
    return slots;
}

std::vector<TakeSlot> plan_materials(std::span<const std::string> takes,
                                     std::span<const std::string> timelines) {
    auto slots = plan_takes(takes);
    slots.reserve(takes.size() + timelines.size());
    for (std::size_t index = 0; index < timelines.size(); ++index)
        slots.push_back({takes.size() + index, timelines[index], MaterialKind::timeline, index});
    return slots;
}

std::string run_id_for_position(std::string_view base_run_id, std::size_t position) {
    std::string run_id{base_run_id};
    if (position != 0U)
        run_id += "-take-" + std::to_string(position + 1U);
    return run_id;
}

std::string run_id_for_slot(std::string_view base_run_id, const TakeSlot &slot) {
    if (slot.kind == MaterialKind::take)
        return run_id_for_position(base_run_id, slot.position);
    std::string run_id{base_run_id};
    if (slot.position != 0U)
        run_id += "-timeline-" + std::to_string(slot.position + 1U);
    return run_id;
}

} // namespace ayther::audio_qa
