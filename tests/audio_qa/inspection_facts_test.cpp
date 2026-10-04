// Spec 002, BR-075 (RF-2.13, RNF-5; contracts.md C2): the new fact classes
// `inspection_event`, `render_frame` and `render_summary` travel in `fact_batch`, are
// written in `fragments/` with the current 1.4 record and are read back by class from a
// run directory. (Terminal 1.4 and the reading of 1.3 are in replay_execution_result.)
#include "exclusive_evidence_directory.h"
#include "fact_batch.h"
#include "fact_fragment_store.h"
#include "inspection_facts.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::FactField known(std::string name, qa::FactFieldValue value,
                    qa::FactFieldUnit unit = qa::FactFieldUnit::none) {
    return {std::move(name), qa::Availability::known, unit, std::move(value), {}};
}

qa::Fact fact(std::uint64_t sequence, std::string kind, std::vector<qa::FactField> fields) {
    qa::Fact value;
    value.id = {"run-75", "inspection", sequence};
    value.kind = std::move(kind);
    value.fields = std::move(fields);
    return value;
}

std::vector<qa::Fact> sample() {
    return {
        fact(1, "inspection_event",
             {known("seq", std::uint64_t{1}), known("control", std::string{"pause"}),
              known("frame_before", std::uint64_t{4310}), known("frame_after", std::uint64_t{4310}),
              known("visit", std::uint64_t{1}), known("elapsed_ms", std::uint64_t{71850})}),
        fact(2, "render_frame",
             {known("frame", std::uint64_t{4310}, qa::FactFieldUnit::emulation_frame),
              known("visit", std::uint64_t{1}),
              known("composable", true),
              known("occurrences", std::uint64_t{3}, qa::FactFieldUnit::count),
              {"processing_ms", qa::Availability::unknown, qa::FactFieldUnit::none,
               std::monostate{}, "not_measured"}}),
        fact(3, "render_summary",
             {known("frame", std::uint64_t{120}, qa::FactFieldUnit::emulation_frame),
              known("occurrences", std::uint64_t{5}, qa::FactFieldUnit::count),
              known("replaced", std::uint64_t{3}, qa::FactFieldUnit::count),
              known("original_unassigned", std::uint64_t{1}, qa::FactFieldUnit::count),
              known("assigned_not_applied", std::uint64_t{1}, qa::FactFieldUnit::count),
              known("texture_not_ready", std::uint64_t{0}, qa::FactFieldUnit::count)}),
    };
}

void classes_are_read() {
    const auto facts = sample();
    const auto summary = qa::read_render_summary(facts[2]);
    expect(summary && summary->frame == 120U && summary->occurrences == 5U &&
               summary->replaced == 3U && summary->original_unassigned == 1U &&
               summary->assigned_not_applied == 1U && summary->texture_not_ready == 0U,
           "C2: render_summary is read with its counts");
    const auto frame = qa::read_render_frame(facts[1]);
    expect(frame && frame->frame == 4310U && frame->visit == 1U && frame->composable &&
               frame->occurrences == 3U && !frame->processing_ms,
           "RF-7.7: render_frame keeps an unknown measure as unknown");
    auto inconsistent = facts[2];
    inconsistent.fields[2] = known("replaced", std::uint64_t{9}, qa::FactFieldUnit::count);
    expect(!qa::read_render_summary(inconsistent),
           "C2: a summary whose statuses exceed its occurrences is rejected");
    auto non_composable = facts[1];
    non_composable.fields[2] = known("composable", std::string{"raster_split"});
    const auto split = qa::read_render_frame(non_composable);
    expect(split && !split->composable && split->not_composable_reason == "raster_split",
           "C3: a frame that cannot be composed keeps its reason");
}

void batch_and_fragments(const std::filesystem::path &root) {
    const auto facts = sample();
    for (const auto &item : facts)
        expect(qa::well_formed(item), "C2: each new class is a well-formed fact");
    const auto encoded = qa::encode_fact_batch(facts, 7U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    expect(bytes != nullptr, "C1-4: the new classes travel in fact_batch");
    if (bytes != nullptr) {
        const auto decoded = qa::decode_fact_batch(*bytes, 7U);
        expect(std::holds_alternative<std::vector<qa::Fact>>(decoded) &&
                   std::get<std::vector<qa::Fact>>(decoded) == facts,
               "C1-4: the batch decodes to the same facts");
    }
    const auto reserved = qa::create_exclusive_evidence_directory(root, "run-75");
    const auto *directory = std::get_if<qa::ExclusiveEvidenceDirectory>(&reserved);
    expect(directory != nullptr, "the run directory is reserved");
    if (directory == nullptr)
        return;
    const auto written = qa::write_fact_fragment(*directory, 1U, facts);
    expect(std::holds_alternative<qa::StoredFactFragment>(written),
           "C2: the new classes are written in fragments with the 1.4 record");
    const auto read = qa::read_run_inspection_facts(directory->path());
    const auto *found = std::get_if<qa::RunInspectionFacts>(&read);
    expect(found != nullptr && found->inspection_events.size() == 1U &&
               found->render_frames.size() == 1U && found->render_summaries.size() == 1U &&
               found->inspection_events.front().control == "pause",
           "C2: the run directory is read back by class");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-inspection-facts";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    classes_are_read();
    batch_and_fragments(root);
    std::filesystem::remove_all(root, ignored);
    if (failures != 0)
        return 1;
    std::cout << "the new fact classes are written and read back\n";
    return 0;
}
