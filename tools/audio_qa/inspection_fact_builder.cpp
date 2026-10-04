#include "inspection_fact_builder.h"

#include <utility>

namespace ayther::audio_qa {
namespace {

FactField known(std::string name, FactFieldValue value, FactFieldUnit unit = FactFieldUnit::none) {
    return {std::move(name), Availability::known, unit, std::move(value), {}};
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

} // namespace ayther::audio_qa
