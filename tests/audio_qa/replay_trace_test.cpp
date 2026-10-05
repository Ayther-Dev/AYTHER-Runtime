#include "fact_trace_summary.h"
#include "inspection_fact_builder.h"
#include "replay_trace.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;
namespace observation = ayther::engine::audio_observation;

namespace {

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error{message};
}

void emit(qa::ReplayTraceCollector &collector, const observation::FactId id,
          const std::string_view kind, const std::span<const observation::Cause> causes = {},
          const std::uint64_t occurrence = 0U) {
    const observation::FieldView field{"occurrence",
                                       observation::Availability::known,
                                       observation::Unit::none,
                                       observation::OccurrenceId{occurrence},
                                       {}};
    collector.consume("run-171", observation::FactView{
                                     id,
                                     kind,
                                     {},
                                     causes,
                                     {},
                                     occurrence == 0U ? std::span<const observation::FieldView>{}
                                                      : std::span{&field, 1U}});
}

// Spec 002, DI-15 (D-11): a recovery produces silently and its Engine facts are not evidence
// (plan D14). A gap the run declares as an exclusion is not a loss; a gap it does not declare
// still is, and a cause that resolves to an excluded fact counts as excluded, not unresolved.
int declared_exclusions(const std::vector<qa::Fact> &connected) {
    int failures = 0;
    const auto expect = [&failures](const bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        }
    };
    const auto fact = [](const std::uint64_t sequence, const std::vector<std::uint64_t> &causes) {
        const std::vector<observation::Cause> engine_causes = [&causes] {
            std::vector<observation::Cause> result;
            for (const auto cause : causes)
                result.emplace_back(observation::FactId{9, cause});
            return result;
        }();
        const observation::FactView view{
            {9, sequence}, "frame_observation", {}, engine_causes, {}, {}};
        auto copied = qa::copy_replay_trace_fact("run-171", view);
        if (!copied)
            throw std::runtime_error{"exclusion_fixture_rejected"};
        return *copied;
    };
    const auto exclusion = [](const std::uint64_t inspection_sequence, const std::uint64_t from,
                              const std::uint64_t to, std::string cause = "silent_recovery") {
        return qa::make_fact_exclusion_fact(
            "run-171", inspection_sequence,
            qa::FactExclusion{1U, "engine-9", from, to, std::move(cause)});
    };
    // Producer 9: 1 to 3 before the recovery, 4 to 7 produced silently (absent), 8 to 10 after
    // the resume; 8 is caused by 5, a fact of the recovery.
    auto trace = connected;
    for (const std::uint64_t sequence : {1U, 2U, 3U})
        trace.push_back(fact(sequence, {}));
    trace.push_back(fact(8U, {5U}));
    trace.push_back(fact(9U, {8U}));
    trace.push_back(fact(10U, {}));
    const auto summary = [](std::vector<qa::Fact> facts) {
        return qa::summarize_replay_facts(facts, true);
    };

    expect(!summary(trace).loss_free, "DI-15: an undeclared gap of sequences is still a loss");
    auto declared = trace;
    declared.push_back(exclusion(1U, 4U, 7U));
    const auto excluded = summary(declared);
    expect(excluded.loss_free,
           "DI-15: a gap declared as an exclusion of a silent recovery is not a loss");
    expect(excluded.causally_connected,
           "DI-15: the declared exclusion leaves the trace causally connected");
    expect(excluded.observed_fact_count == declared.size(),
           "DI-15: the declaration is counted as one more fact of the run");
    auto declared_first = declared;
    std::rotate(declared_first.begin(), declared_first.end() - 1, declared_first.end());
    expect(summary(declared_first).loss_free,
           "DI-15: a declaration read before the facts it explains still excludes them");

    auto partial = trace;
    partial.push_back(exclusion(1U, 4U, 6U));
    expect(!summary(partial).loss_free, "DI-15: a gap only partly declared is still a loss");
    auto covering = trace;
    covering.push_back(exclusion(1U, 3U, 7U));
    expect(!summary(covering).loss_free,
           "DI-15: a declaration that covers a fact kept in the trace is not an exclusion");
    auto unexplained_cause = declared;
    unexplained_cause.push_back(fact(11U, {20U}));
    expect(!summary(unexplained_cause).loss_free,
           "DI-15: a cause that is neither kept nor declared is still unresolved");
    auto other_cause = trace;
    other_cause.push_back(exclusion(1U, 4U, 7U, "frame_dropped"));
    expect(!summary(other_cause).loss_free,
           "DI-15: only the cause silent_recovery declares an exclusion");
    auto twice = declared;
    twice.push_back(exclusion(2U, 6U, 7U));
    expect(!summary(twice).loss_free, "DI-15: a sequence is excluded only once");
    return failures;
}

} // namespace

int main() {
    try {
        qa::ReplayTraceCollector collector{"run-171"};
        emit(collector, {3, 1}, "detector_input");
        const std::array<observation::Cause, 1> ingress{observation::FactId{3, 1}};
        emit(collector, {5, 1}, "assignment_candidate", ingress);
        const std::array<observation::Cause, 1> candidate{observation::FactId{5, 1}};
        emit(collector, {5, 2}, "assignment_selection", candidate);
        const std::array<observation::Cause, 1> selection{observation::FactId{5, 2}};
        emit(collector, {5, 3}, "hd_playback_request", selection, 7);
        const std::array<observation::Cause, 1> request{observation::FactId{5, 3}};
        emit(collector, {5, 4}, "hd_playback_decision", request, 7);
        const std::array<observation::Cause, 1> decision{observation::FactId{5, 4}};
        emit(collector, {5, 5}, "hd_playback_effect", decision, 7);
        emit(collector, {6, 1}, "hd_mix_participant", request, 7);
        const std::array<observation::Cause, 2> output_causes{observation::FactId{6, 1},
                                                              observation::FactId{7, 1}};
        emit(collector, {8, 1}, "main_mix_output_span", output_causes, 7);

        const auto summary = collector.summarize(true);
        require(summary.causally_connected && summary.loss_free, "connected_trace_not_recognized");
        require(summary.ingress == qa::ReplayTraceFactId{3, 1} &&
                    summary.mix_span == qa::ReplayTraceFactId{6, 1} && summary.occurrence == 7,
                "trace_identity_changed");
        require(!collector.summarize(false).causally_connected, "lossy_trace_was_accepted");
        const auto durable_summary = qa::summarize_replay_facts(collector.facts(), true);
        require(durable_summary == summary, "durable_trace_summary_changed");
        auto reordered = collector.facts();
        std::rotate(reordered.begin(), reordered.begin() + 1, reordered.end());
        const auto reordered_summary = qa::summarize_replay_facts(reordered, true);
        require(reordered_summary == summary, "cross_producer_arrival_order_changed_trace");

        // Spec 002, BR-149 (contracts.md C2): the Runtime's own inspection facts travel with
        // the trace without taking part in it; any other producer is still rejected.
        auto inspected = collector.facts();
        inspected.push_back(qa::make_inspection_event_fact(
            "run-171", 1, qa::InspectionEvent{1, "pause", 5, 5, 1, 100}));
        inspected.push_back(qa::make_inspection_event_fact(
            "run-171", 2, qa::InspectionEvent{2, "resume", 5, 5, 1, 100}));
        auto with_inspection = summary;
        with_inspection.observed_fact_count += 2U;
        require(qa::summarize_replay_facts(inspected, true) == with_inspection,
                "inspection_facts_changed_the_audio_trace");
        auto intruder = qa::make_inspection_event_fact("run-171", 1,
                                                       qa::InspectionEvent{1, "pause", 5, 5, 1, 0});
        intruder.id.producer_id = "runtime-overlay";
        auto foreign = collector.facts();
        foreign.push_back(intruder);
        require(!qa::summarize_replay_facts(foreign, true).loss_free,
                "a fact of an unknown producer was accepted");
        auto disguised = qa::make_inspection_event_fact(
            "run-171", 3, qa::InspectionEvent{3, "pause", 5, 5, 1, 0});
        disguised.kind = "playback_effect";
        auto disguised_trace = collector.facts();
        disguised_trace.push_back(disguised);
        require(!qa::summarize_replay_facts(disguised_trace, true).loss_free,
                "the inspection producer can only carry inspection classes");

        auto unresolved_output = collector.facts();
        const std::array<observation::Cause, 2> invalid_output_causes{observation::FactId{6, 1},
                                                                      observation::FactId{9, 1}};
        const observation::FactView invalid_output_view{
            {8, 2}, "main_mix_output_span", {}, invalid_output_causes, {}, {}};
        const auto invalid_output = qa::copy_replay_trace_fact("run-171", invalid_output_view);
        require(invalid_output.has_value(), "invalid_output_fixture_rejected");
        unresolved_output.push_back(*invalid_output);
        require(!qa::summarize_replay_facts(unresolved_output, true).loss_free,
                "missing_non_pcm_output_cause_was_ignored");
        const auto &durable_ingress = collector.facts().front();
        require(durable_ingress.decision_id.reason_code == "compact_default" &&
                    durable_ingress.assignment_id.reason_code == "compact_default" &&
                    durable_ingress.occurrence_id.reason_code == "compact_default" &&
                    durable_ingress.reason_code.reason_code == "compact_default" &&
                    durable_ingress.shared_state_order.reason_code == "compact_default",
                "durable_not_applicable_fields_were_not_compacted");

        const std::array detail_fields{
            observation::FieldView{"mix_begin",
                                   observation::Availability::known,
                                   observation::Unit::sample_frame,
                                   std::uint64_t{4096},
                                   {}},
            observation::FieldView{"effective_gain",
                                   observation::Availability::known,
                                   observation::Unit::linear_gain,
                                   0.5,
                                   {}},
            observation::FieldView{"track",
                                   observation::Availability::known,
                                   observation::Unit::none,
                                   std::string_view{"music"},
                                   {}},
            observation::FieldView{"selection",
                                   observation::Availability::known,
                                   observation::Unit::none,
                                   observation::FactId{5, 2},
                                   {}},
            observation::FieldView{"output_position", observation::Availability::unknown,
                                   observation::Unit::sample_frame, std::monostate{},
                                   "frame_timeline_only"}};
        const observation::FactView detailed_view{{6, 2}, "hd_mix_participant", {}, request,
                                                  {},     detail_fields};
        const auto detailed = qa::copy_replay_trace_fact("run-171", detailed_view);
        require(detailed && detailed->fields.size() == detail_fields.size() &&
                    std::get<std::uint64_t>(detailed->fields[0].value) == 4096U &&
                    std::get<double>(detailed->fields[1].value) == 0.5 &&
                    std::get<std::string>(detailed->fields[2].value) == "music" &&
                    std::get<qa::FactId>(detailed->fields[3].value) ==
                        qa::FactId{"run-171", "engine-5", 2} &&
                    detailed->fields[4].availability == qa::Availability::unknown &&
                    detailed->fields[4].unavailable_reason == "frame_timeline_only",
                "durable_fact_lost_typed_observation_fields");

        const std::array<std::byte, 16> pcm_bytes{};
        const std::array<observation::Cause, 1> pcm_causes{observation::FactId{6, 2}};
        const observation::PcmView pcm_view{{7, 1},
                                            "sdl_logical_device_postmix",
                                            {"engine_main_output", 44100, 0, 2},
                                            observation::PcmFormat::f32_le,
                                            2,
                                            pcm_bytes,
                                            pcm_causes};
        const auto pcm = qa::copy_replay_trace_pcm("run-171", pcm_view);
        require(pcm && pcm->cause_ids == std::vector<qa::FactId>{{"run-171", "engine-6", 2}},
                "durable_pcm_lost_observed_causes");

        qa::ReplayTraceCollector long_trace{"run-171"};
        for (std::uint64_t sequence = 1; sequence <= 5000; ++sequence)
            emit(long_trace, {9, sequence}, "frame_observation");
        emit(long_trace, {3, 1}, "detector_input");
        const std::array<observation::Cause, 1> long_ingress{observation::FactId{3, 1}};
        emit(long_trace, {5, 1}, "assignment_candidate", long_ingress);
        const std::array<observation::Cause, 1> long_candidate{observation::FactId{5, 1}};
        emit(long_trace, {5, 2}, "assignment_selection", long_candidate);
        const std::array<observation::Cause, 1> long_selection{observation::FactId{5, 2}};
        emit(long_trace, {5, 3}, "hd_playback_request", long_selection, 11);
        const std::array<observation::Cause, 1> long_request{observation::FactId{5, 3}};
        emit(long_trace, {5, 4}, "hd_playback_decision", long_request, 11);
        const std::array<observation::Cause, 1> long_decision{observation::FactId{5, 4}};
        emit(long_trace, {5, 5}, "hd_playback_effect", long_decision, 11);
        emit(long_trace, {6, 1}, "hd_mix_participant", long_request, 11);
        const auto long_summary = long_trace.summarize(true);
        require(long_trace.valid() && long_summary.causally_connected &&
                    long_summary.observed_fact_count == 5007U && long_summary.occurrence == 11U,
                "trace_above_old_4096_limit_was_lost");

        if (declared_exclusions(collector.facts()) != 0)
            throw std::runtime_error{"declared_exclusions_failed"};

        qa::ReplayTraceCollector wrong_run{"run-171"};
        const observation::FactView fact{{3, 1}, "detector_input", {}, {}, {}, {}};
        wrong_run.consume("another-run", fact);
        require(!wrong_run.valid(), "foreign_run_was_accepted");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_trace_test: %s\n", error.what());
        return 1;
    }
}
