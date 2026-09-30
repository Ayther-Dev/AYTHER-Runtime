#include "fact_trace_summary.h"
#include "replay_trace.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
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
