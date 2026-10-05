#include "inspection_facts.h"

#include "fact_fragment_store.h"
#include "long_path.h"

#include <algorithm>
#include <system_error>

namespace ayther::audio_qa {
namespace {

const FactField *find(const Fact &fact, std::string_view name) {
    const auto found = std::find_if(fact.fields.begin(), fact.fields.end(),
                                    [name](const FactField &field) { return field.name == name; });
    return found == fact.fields.end() ? nullptr : &*found;
}

std::optional<std::uint64_t> count(const Fact &fact, std::string_view name) {
    const auto *field = find(fact, name);
    if (field == nullptr || field->availability != Availability::known)
        return std::nullopt;
    if (const auto *value = std::get_if<std::uint64_t>(&field->value))
        return *value;
    return std::nullopt;
}

// A measure is known, or explicitly not known; an absent field is an invalid fact.
bool measure(const Fact &fact, std::string_view name, std::optional<double> &out) {
    const auto *field = find(fact, name);
    if (field == nullptr)
        return true;
    if (field->availability != Availability::known)
        return true;
    if (const auto *value = std::get_if<double>(&field->value)) {
        out = *value;
        return true;
    }
    return false;
}

} // namespace

std::optional<RenderFrameFact> read_render_frame(const Fact &fact) {
    if (fact.kind != "render_frame")
        return std::nullopt;
    const auto frame = count(fact, "frame");
    const auto visit = count(fact, "visit");
    const auto occurrences = count(fact, "occurrences");
    const auto *composable = find(fact, "composable");
    if (!frame || !visit || !occurrences || composable == nullptr ||
        composable->availability != Availability::known)
        return std::nullopt;
    RenderFrameFact result;
    result.frame = *frame;
    result.visit = *visit;
    result.occurrences = *occurrences;
    // C2: `composable`, or the reason why the frame cannot be composed.
    if (const auto *flag = std::get_if<bool>(&composable->value)) {
        if (!*flag)
            return std::nullopt;
        result.composable = true;
    } else if (const auto *reason = std::get_if<std::string>(&composable->value)) {
        if (reason->empty())
            return std::nullopt;
        result.not_composable_reason = *reason;
    } else {
        return std::nullopt;
    }
    if (!measure(fact, "processing_ms", result.processing_ms) ||
        !measure(fact, "fps_instant", result.fps_instant))
        return std::nullopt;
    return result;
}

std::optional<RenderSummaryFact> read_render_summary(const Fact &fact) {
    if (fact.kind != "render_summary")
        return std::nullopt;
    const auto frame = count(fact, "frame");
    const auto occurrences = count(fact, "occurrences");
    const auto replaced = count(fact, "replaced");
    const auto unassigned = count(fact, "original_unassigned");
    const auto not_applied = count(fact, "assigned_not_applied");
    const auto not_ready = count(fact, "texture_not_ready");
    if (!frame || !occurrences || !replaced || !unassigned || !not_applied || !not_ready)
        return std::nullopt;
    // Every occurrence has one status; a texture not ready is one way of not applying.
    if (*replaced + *unassigned + *not_applied > *occurrences || *not_ready > *not_applied)
        return std::nullopt;
    return RenderSummaryFact{*frame,      *occurrences, *replaced,
                             *unassigned, *not_applied, *not_ready};
}

RunInspectionFactsResult read_run_inspection_facts(const std::filesystem::path &run_directory) {
    std::vector<std::filesystem::path> fragments;
    std::error_code error;
    for (std::filesystem::directory_iterator entry{long_path(run_directory / "fragments"), error},
         end;
         !error && entry != end; entry.increment(error))
        if (entry->path().extension() == ".aqf")
            fragments.push_back(entry->path());
    if (error)
        return InspectionFactsError::unreadable_fragment;
    // The fragment names carry their zero-padded sequence.
    std::sort(fragments.begin(), fragments.end());
    RunInspectionFacts result;
    for (const auto &path : fragments) {
        const auto read = read_fact_fragment(path);
        const auto *fragment = std::get_if<StoredFactFragment>(&read);
        if (fragment == nullptr)
            return InspectionFactsError::unreadable_fragment;
        for (const auto &fact : fragment->facts) {
            if (fact.kind == "inspection_event") {
                auto event = read_inspection_event(fact);
                if (!event)
                    return InspectionFactsError::invalid_fact;
                result.inspection_events.push_back(std::move(*event));
            } else if (fact.kind == "render_frame") {
                auto frame = read_render_frame(fact);
                if (!frame)
                    return InspectionFactsError::invalid_fact;
                result.render_frames.push_back(std::move(*frame));
            } else if (fact.kind == "render_summary") {
                const auto summary = read_render_summary(fact);
                if (!summary)
                    return InspectionFactsError::invalid_fact;
                result.render_summaries.push_back(*summary);
            }
        }
    }
    return result;
}

} // namespace ayther::audio_qa
