#include "incremental_evidence.h"
#include "integrated_evidence.h"
#include "pcm_message.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
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
    std::filesystem::remove_all(path, ignored);
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

        remove_tree(fixture);
        std::puts("integrated_evidence_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "integrated_evidence_test: %s\n", error.what());
        return 1;
    }
}
