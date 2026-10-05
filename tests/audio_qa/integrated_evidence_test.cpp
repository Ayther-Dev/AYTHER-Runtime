#include "durable_file.h"
#include "incremental_evidence.h"
#include "integrated_evidence.h"
#include "long_path.h"
#include "pcm_message.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error{message};
}

qa::Fact fact(const std::uint64_t sequence) {
    qa::Fact value;
    value.id = {"run-integrated-partial", "engine-9", sequence};
    value.kind = "frame_observation";
    return value;
}

qa::AudioChunk audio_chunk() {
    qa::AudioChunk chunk;
    chunk.run_id = "run-integrated-partial";
    chunk.capture_point = "session-postmix";
    chunk.producer_sequence = 1U;
    chunk.format = {qa::PcmFormat::f32le, 44100U, 2U};
    chunk.range = {"engine-main-output", 44100U, 0U, 2U};
    chunk.bytes.resize(2U * 2U * sizeof(float));
    chunk.sha256 = {qa::Availability::known, qa::pcm_sha256(chunk.bytes), {}};
    chunk.durability = qa::Durability::pending;
    chunk.checkpoint_id = {qa::Availability::not_applicable, std::nullopt, "not_published"};
    return chunk;
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    // D-12: the fixture holds paths longer than MAX_PATH.
    std::filesystem::remove_all(qa::long_path(path), ignored);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-integrated-evidence";
    remove_tree(fixture);
    try {
        std::vector<qa::Fact> facts;
        facts.reserve(1500U);
        for (std::uint64_t sequence = 1U; sequence <= 1500U; ++sequence)
            facts.push_back(fact(sequence));
        const std::vector chunks{audio_chunk()};
        qa::ReplayTraceSummary trace;
        trace.observed_fact_count = facts.size();

        const auto result = qa::persist_and_reopen_evidence(fixture, "run-integrated-partial",
                                                            facts, chunks, trace);
        const auto *summary = std::get_if<qa::IntegratedEvidenceSummary>(&result);
        require(summary != nullptr, "partial_evidence_was_not_preserved");
        require(summary->facts == facts.size() && summary->pcm_blocks == 1U &&
                    summary->pcm_bytes == chunks.front().bytes.size() &&
                    !summary->relationships_reopened,
                "partial_evidence_summary_changed");

        std::size_t fragment_count{};
        for (const auto &entry :
             std::filesystem::directory_iterator(summary->directory / "fragments"))
            if (entry.is_regular_file() && entry.path().extension() == ".aqf")
                ++fragment_count;
        require(fragment_count >= 2U, "large_trace_was_not_split_into_fragments");

        const auto streaming_root = fixture / "streaming";
        auto opened = qa::open_incremental_evidence(streaming_root, "run-streaming");
        auto *writer = std::get_if<qa::IncrementalEvidenceWriter>(&opened);
        require(writer != nullptr, "incremental_evidence_was_not_opened");
        const auto make_trace_fact = [](const char *producer, const std::uint64_t sequence,
                                        const char *kind,
                                        std::optional<qa::FactId> cause = std::nullopt,
                                        const std::uint64_t occurrence = 0U) {
            qa::Fact value;
            value.id = {"run-streaming", producer, sequence};
            value.kind = kind;
            if (cause)
                value.cause_ids.emplace_back(std::move(*cause));
            if (occurrence != 0U)
                value.occurrence_id = {qa::Availability::known, std::to_string(occurrence), {}};
            return value;
        };
        const std::array producer_five{
            make_trace_fact("engine-5", 1U, "assignment_candidate",
                            qa::FactId{"run-streaming", "engine-3", 1U}),
            make_trace_fact("engine-5", 2U, "assignment_selection",
                            qa::FactId{"run-streaming", "engine-5", 1U}),
            make_trace_fact("engine-5", 3U, "hd_playback_request",
                            qa::FactId{"run-streaming", "engine-5", 2U}, 7U),
            make_trace_fact("engine-5", 4U, "hd_playback_decision",
                            qa::FactId{"run-streaming", "engine-5", 3U}, 7U),
            make_trace_fact("engine-5", 5U, "hd_playback_effect",
                            qa::FactId{"run-streaming", "engine-5", 4U}, 7U)};
        const std::array mix{make_trace_fact("engine-6", 1U, "hd_mix_participant",
                                             qa::FactId{"run-streaming", "engine-5", 3U}, 7U)};
        const std::array ingress{make_trace_fact("engine-3", 1U, "detector_input")};
        const std::array late_sequence{make_trace_fact("engine-9", 2U, "auxiliary_output_span"),
                                       make_trace_fact("engine-9", 1U, "auxiliary_output_span")};
        require(!writer->append_facts(producer_five) && !writer->append_facts(mix) &&
                    !writer->append_facts(ingress) &&
                    !writer->append_facts(std::span{late_sequence}.first(1U)) &&
                    !writer->append_facts(std::span{late_sequence}.last(1U)),
                "incremental_fact_batch_was_not_published");
        auto streamed_pcm = audio_chunk();
        streamed_pcm.run_id = "run-streaming";
        require(!writer->append_pcm(streamed_pcm), "incremental_pcm_was_not_published");
        auto next_streamed_pcm = streamed_pcm;
        next_streamed_pcm.producer_sequence = 3U;
        next_streamed_pcm.range.begin = streamed_pcm.range.end;
        next_streamed_pcm.range.end = next_streamed_pcm.range.begin + 2U;
        next_streamed_pcm.sha256 = {
            qa::Availability::known, qa::pcm_sha256(next_streamed_pcm.bytes), {}};
        require(!writer->append_pcm(next_streamed_pcm),
                "interleaved_fact_sequence_was_treated_as_pcm_loss");
        qa::ReplayTraceSummary streamed_transport;
        streamed_transport.observed_fact_count = 9U;
        streamed_transport.loss_free = true;
        const auto streamed = writer->finish(streamed_transport);
        const auto *streamed_summary = std::get_if<qa::IntegratedEvidenceSummary>(&streamed);
        const auto streamed_trace = writer->trace();
        require(streamed_summary != nullptr && streamed_summary->facts == 9U &&
                    streamed_summary->pcm_blocks == 2U &&
                    streamed_summary->fact_integrity_complete &&
                    streamed_summary->relationships_reopened && streamed_trace.occurrence == 7U &&
                    streamed_trace.causally_connected,
                "incremental_evidence_was_not_reopened");

        // Spec 002, DI-14: the evidence of an inspection is written per linear segment. The
        // writer audits continuity within each segment and declares each interval; a jump
        // between segments is not a loss, but a linear traversal still needs one interval.
        const auto segment_chunk = [](const char *run, const std::uint64_t sequence,
                                      const std::uint64_t begin, const std::uint64_t segment) {
            auto chunk = audio_chunk();
            chunk.run_id = run;
            chunk.producer_sequence = sequence;
            chunk.range.begin = begin;
            chunk.range.end = begin + 2U;
            chunk.segment = segment;
            return chunk;
        };
        const auto segment_fact = [](const char *run) {
            qa::Fact value;
            value.id = {run, "engine-9", 1U};
            value.kind = "frame_observation";
            return std::array{value};
        };
        qa::ReplayTraceSummary one_fact;
        one_fact.observed_fact_count = 1U;
        const auto stream_segments = [&](const char *run) {
            auto writer_opened = qa::open_incremental_evidence(fixture / run, run);
            auto *segmented = std::get_if<qa::IncrementalEvidenceWriter>(&writer_opened);
            require(segmented != nullptr && !segmented->append_facts(segment_fact(run)),
                    "segmented_evidence_was_not_opened");
            // Segment 0 up to 4, a step back (1 restarts the line at 1), a step forward
            // (2 starts at 10).
            for (const auto &chunk :
                 {segment_chunk(run, 1U, 0U, 0U), segment_chunk(run, 2U, 2U, 0U),
                  segment_chunk(run, 3U, 1U, 1U), segment_chunk(run, 4U, 10U, 2U)})
                require(!segmented->append_pcm(chunk),
                        "DI-14: a jump between two segments was taken for a PCM loss");
            return std::move(*segmented);
        };
        auto inspected = stream_segments("run-segments");
        require(inspected.pcm_segments() ==
                    std::vector<qa::PcmSegmentInterval>{
                        {0U, {"engine-main-output", 44100U, 0U, 4U}, 2U},
                        {1U, {"engine-main-output", 44100U, 1U, 3U}, 1U},
                        {2U, {"engine-main-output", 44100U, 10U, 12U}, 1U}},
                "DI-14: each segment declares its sample interval");
        const auto inspected_finish =
            inspected.finish(one_fact, false, qa::PcmContinuity::per_segment);
        const auto *inspected_summary =
            std::get_if<qa::IntegratedEvidenceSummary>(&inspected_finish);
        require(inspected_summary != nullptr && inspected_summary->pcm_blocks == 4U,
                "DI-14: contiguous and complete segments are complete evidence");
        auto linear = stream_segments("run-segments-linear");
        const auto linear_finish = linear.finish(one_fact, false, qa::PcmContinuity::single);
        const auto *linear_error = std::get_if<qa::IntegratedEvidenceError>(&linear_finish);
        require(linear_error != nullptr &&
                    *linear_error == qa::IntegratedEvidenceError::pcm_continuity_failed,
                "DI-14: a linear traversal still requires one continuous interval");
        auto gap_opened = qa::open_incremental_evidence(fixture / "gap", "run-segment-gap");
        auto *gap_writer = std::get_if<qa::IncrementalEvidenceWriter>(&gap_opened);
        require(gap_writer != nullptr &&
                    !gap_writer->append_pcm(segment_chunk("run-segment-gap", 1U, 0U, 1U)),
                "segment_gap_evidence_was_not_opened");
        const auto inner_gap = gap_writer->append_pcm(segment_chunk("run-segment-gap", 2U, 3U, 1U));
        require(inner_gap && *inner_gap == qa::IntegratedEvidenceError::pcm_continuity_failed,
                "DI-14: a gap within a segment is a loss");

        // D-12 (campaign 2026-10-05; RF-2.5, RF-2.13, RNF-5): the evidence of a take is kept
        // whatever the length of its destination. Two shapes:
        //  - the campaign one: the run directory fits in MAX_PATH (260) but its fragments do
        //    not (`br190\failure-sequence-rejected-state`, 263 and 270 characters);
        //  - a destination that is itself longer than MAX_PATH.
        const auto padded_root = [&](const char *name, const std::size_t length) {
            auto root = fixture / name;
            std::size_t part{};
            while (root.native().size() + 1U < length) {
                const auto room = length - root.native().size() - 1U;
                root /= std::string((std::min)(room, std::size_t{40U}), char('a' + part % 26U));
                ++part;
            }
            return root;
        };
        const auto long_take = [&](const std::filesystem::path &root, const char *run) {
            auto opened_long = qa::open_incremental_evidence(root, run);
            auto *long_writer = std::get_if<qa::IncrementalEvidenceWriter>(&opened_long);
            require(long_writer != nullptr, "D-12: the evidence directory of a take opens");
            std::vector<qa::Fact> first_batch;
            std::vector<qa::Fact> second_batch;
            for (std::uint64_t sequence = 1U; sequence <= 4U; ++sequence) {
                auto value = fact(sequence);
                value.id.run_id = run;
                (sequence <= 2U ? first_batch : second_batch).push_back(std::move(value));
            }
            // The first batch is written in place, the next ones in the background.
            const auto first_error = long_writer->append_facts(first_batch);
            const auto second_error = long_writer->append_facts(second_batch);
            require(!first_error && !second_error,
                    "D-12: the facts of a take are published under a long destination");
            auto chunk = audio_chunk();
            chunk.run_id = run;
            require(!long_writer->append_pcm(chunk),
                    "D-12: the PCM of a take is published under a long destination");
            qa::ReplayTraceSummary long_trace;
            long_trace.observed_fact_count = 4U;
            long_trace.loss_free = true;
            const auto long_finish = long_writer->finish(long_trace, false);
            const auto *long_summary = std::get_if<qa::IntegratedEvidenceSummary>(&long_finish);
            require(long_summary != nullptr && long_summary->facts == 4U &&
                        long_summary->pcm_blocks == 1U && long_summary->fact_integrity_complete,
                    "D-12: the evidence of a take under a long destination is reopened whole");
            const auto run_directory = root / "runs" / run;
            const std::string result_text{"code = 'replay_evidence_streamed'\n"};
            const auto published = qa::publish_durable_file(
                run_directory / "replay-result.toml",
                std::as_bytes(std::span{result_text.data(), result_text.size()}));
            require(std::holds_alternative<qa::DurablePublishedFile>(published),
                    "D-12: the result of a take is published under a long destination");
            return (run_directory / "fragments" / "facts-00000000000000000001.aqf").native().size();
        };
        const char *const campaign_run = "run-5d5f960bf693f8de7c444b52a57e7fcc-take-2";
        const auto campaign_root = padded_root("campaign", 180U);
        std::filesystem::create_directories(campaign_root);
        require(campaign_root.native().size() == 180U &&
                    (campaign_root / "runs" / campaign_run).native().size() < 248U,
                "D-12: the campaign destination is reproduced");
        require(long_take(campaign_root, campaign_run) > 260U,
                "D-12: the campaign fragment path passes MAX_PATH");
        long_take(padded_root("beyond", 300U), "run-beyond-max-path");

        remove_tree(fixture);
        std::puts("integrated_evidence_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "integrated_evidence_test: %s\n", error.what());
        return 1;
    }
}
