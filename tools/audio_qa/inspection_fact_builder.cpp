#include "inspection_fact_builder.h"

#include <algorithm>
#include <utility>

namespace ayther::audio_qa {
namespace {

FactField known(std::string name, FactFieldValue value, FactFieldUnit unit = FactFieldUnit::none) {
    return {std::move(name), Availability::known, unit, std::move(value), {}};
}

const FactField *field_named(const Fact &fact, const std::string_view name) {
    const auto found = std::find_if(fact.fields.begin(), fact.fields.end(),
                                    [name](const FactField &field) { return field.name == name; });
    return found == fact.fields.end() || found->availability != Availability::known ? nullptr
                                                                                    : &*found;
}

template <class T> const T *known_value(const Fact &fact, const std::string_view name) {
    const auto *field = field_named(fact, name);
    return field == nullptr ? nullptr : std::get_if<T>(&field->value);
}

} // namespace

Fact make_inspection_event_fact(std::string run_id, std::uint64_t producer_sequence,
                                const InspectionEvent &event) {
    Fact fact;
    fact.id = {std::move(run_id), std::string{inspection_producer}, producer_sequence};
    fact.kind = "inspection_event";
    fact.frame_index = {Availability::known, event.frame_after, {}};
    fact.fields = {known("seq", event.seq, FactFieldUnit::count),
                   known("control", event.control),
                   known("frame_before", event.frame_before, FactFieldUnit::emulation_frame),
                   known("frame_after", event.frame_after, FactFieldUnit::emulation_frame),
                   known("visit", event.visit, FactFieldUnit::count),
                   known("elapsed_ms", event.elapsed_ms)};
    return fact;
}

Fact make_render_frame_fact(std::string run_id, std::uint64_t producer_sequence,
                            const RenderFrameRecord &record) {
    Fact fact;
    fact.id = {std::move(run_id), std::string{inspection_producer}, producer_sequence};
    fact.kind = "render_frame";
    fact.frame_index = {Availability::known, record.frame, {}};
    fact.fields = {known("frame", record.frame, FactFieldUnit::emulation_frame),
                   known("visit", record.visit, FactFieldUnit::count),
                   record.not_composable_reason.empty()
                       ? known("composable", true)
                       : known("composable", record.not_composable_reason),
                   known("occurrences", record.occurrences, FactFieldUnit::count)};
    // RF-7.7: a measure that does not exist is not known, never zero.
    const auto measure = [&fact](std::string name, const std::optional<double> &value) {
        if (value)
            fact.fields.push_back(known(std::move(name), *value));
        else
            fact.fields.push_back({std::move(name), Availability::not_applicable,
                                   FactFieldUnit::none, std::monostate{}, "not_measured"});
    };
    measure("processing_ms", record.processing_ms);
    measure("fps_instant", record.fps_instant);
    return fact;
}

Fact make_fact_exclusion_fact(std::string run_id, std::uint64_t producer_sequence,
                              const FactExclusion &exclusion) {
    Fact fact;
    fact.id = {std::move(run_id), std::string{inspection_producer}, producer_sequence};
    fact.kind = "fact_exclusion";
    fact.fields = {known("recovery", exclusion.recovery, FactFieldUnit::count),
                   known("producer", exclusion.producer),
                   known("sequence_from", exclusion.sequence_from, FactFieldUnit::count),
                   known("sequence_to", exclusion.sequence_to, FactFieldUnit::count),
                   known("cause", exclusion.cause)};
    return fact;
}

std::optional<FactExclusion> read_fact_exclusion(const Fact &fact) {
    if (fact.kind != "fact_exclusion" || fact.id.producer_id != inspection_producer)
        return std::nullopt;
    const auto *recovery = known_value<std::uint64_t>(fact, "recovery");
    const auto *producer = known_value<std::string>(fact, "producer");
    const auto *from = known_value<std::uint64_t>(fact, "sequence_from");
    const auto *to = known_value<std::uint64_t>(fact, "sequence_to");
    const auto *cause = known_value<std::string>(fact, "cause");
    if (recovery == nullptr || producer == nullptr || from == nullptr || to == nullptr ||
        cause == nullptr || *recovery == 0U || !producer->starts_with("engine-") || *from == 0U ||
        *from > *to || *cause != silent_recovery_cause)
        return std::nullopt;
    return FactExclusion{*recovery, *producer, *from, *to, *cause};
}

} // namespace ayther::audio_qa
