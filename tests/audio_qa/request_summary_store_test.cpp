// Spec 002, BR-066 (RF-2.3, RNF-5; contracts.md C2): the confirmed request summary is
// written durably, read back unchanged, and a major version it does not know is rejected
// without modifying it. Also covers the reading part of BR-077 for this document.
#include "request_summary_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::RequestSummaryDocument document() {
    qa::RequestSummaryDocument value;
    value.request_id = "request-1";
    value.conditions_id = "conditions-sha256-ab";
    value.run_id = "run-1";
    value.exit_code = 2;
    value.linear_complete = false;
    value.values = {{"pack", "none", qa::ValueSource::explicit_option},
                    {"core", "C:/cores/c.dll", qa::ValueSource::environment}};
    value.materials = {{"rom", "--rom", "C:/roms/g.md", std::string(64, 'e'), 524288U}};
    value.takes = {
        {0,
         "C:/t/a.ayr",
         std::string(64, 'a'),
         "run-1",
         "complete",
         "replay_evidence_complete",
         "natural_end",
         "linear",
         "complete",
         {}},
        {1,
         "C:/t/a.ayr",
         std::string(64, 'a'),
         "run-1-take-2",
         "incomplete",
         "replay_cancelled",
         "cancelled",
         "linear",
         "incomplete",
         {"audio_trace_incomplete_empty_pcm"}},
        {2,
         "C:/t/b.ayr",
         std::string(64, 'b'),
         "",
         "incomplete",
         "not_started_after_cancellation",
         "not_started",
         "none",
         "incomplete",
         {}},
    };
    return value;
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-request-summary";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    const auto path = qa::request_summary_path(root, "run-1");
    expect(path == root / "requests" / "run-1" / "request-summary.toml",
           "RF-2.3: the summary lives next to the request it confirms");
    expect(std::holds_alternative<qa::RequestSummaryReadError>(qa::read_request_summary(path)) &&
               std::get<qa::RequestSummaryReadError>(qa::read_request_summary(path)) ==
                   qa::RequestSummaryReadError::missing,
           "RF-2.3: an unconfirmed request has no summary");

    const auto written = qa::write_request_summary(path, document());
    expect(std::holds_alternative<qa::DurablePublishedFile>(written),
           "RNF-5: the summary is published durably");
    const auto read = qa::read_request_summary(path);
    const auto *stored = std::get_if<qa::RequestSummaryDocument>(&read);
    expect(stored != nullptr && *stored == document(),
           "RF-2.3: the summary reads back exactly as confirmed");
    const auto text = read_text(path);
    expect(text.find("schema_version = 1") != std::string::npos &&
               text.find("schema_minor = 1") != std::string::npos &&
               text.find("pack = 'none'") != std::string::npos &&
               text.find("visual_evaluation = 'not_evaluated'") != std::string::npos,
           "RF-1.3, RF-2.7: «Sin pack» and the absent visual evaluation are stated");

    const auto technical = qa::technical_summary(document());
    expect(technical && technical->exit_code == 2 && technical->cancelled == 1U &&
               technical->not_started == 1U && !technical->linear_complete,
           "RF-2.3: the stored summary rebuilds the same console summary");

    // RNF-5: an unknown major is rejected and the file stays as it was.
    std::string future = text;
    future.replace(future.find("schema_version = 1"), 18, "schema_version = 2");
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << future;
    }
    const auto rejected = qa::read_request_summary(path);
    expect(std::holds_alternative<qa::RequestSummaryReadError>(rejected) &&
               std::get<qa::RequestSummaryReadError>(rejected) ==
                   qa::RequestSummaryReadError::unsupported_version &&
               read_text(path) == future,
           "RNF-5: an unknown major is rejected without modifying it");
    expect(std::get<qa::RequestSummaryReadError>(qa::parse_request_summary("kind = 3")) ==
               qa::RequestSummaryReadError::invalid,
           "RNF-5: a malformed summary is invalid");

    std::filesystem::remove_all(root, ignored);
    if (failures != 0)
        return 1;
    std::cout << "request summaries are durable and read back unchanged\n";
    return 0;
}
