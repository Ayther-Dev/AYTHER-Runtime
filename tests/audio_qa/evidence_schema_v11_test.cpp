// Spec 002, BR-077 (RNF-5; contracts.md C2 «Compatibilidad y recuperación»): the new
// evidence documents are read in every version that exists (terminal 1.2, 1.3 and 1.4;
// summary and traversal 1.1), an unknown major is rejected without modifying anything,
// and a cut during any durable write leaves the last confirmed version readable.
#include "inspection_evidence.h"
#include "protocol_header.h"
#include "replay_execution_result.h"
#include "request_summary_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
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

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::vector<std::byte> message(const std::string &payload) {
    const auto header = qa::encode_protocol_header(
        {qa::MessageType::terminal, static_cast<std::uint32_t>(payload.size()), 2U, 2U});
    std::vector<std::byte> bytes(header.begin(), header.end());
    for (const char character : payload)
        bytes.push_back(static_cast<std::byte>(character));
    return bytes;
}

qa::ReplayExecutionResult terminal() {
    qa::ReplayExecutionResult value;
    value.run_id = "run-77";
    value.take_id = "take.ayr";
    value.recording_frames = 6;
    value.inputs_consumed = 6;
    value.initial_game_state.byte_size = 16;
    value.final_game_state.byte_size = 16;
    value.trace.observed_fact_count = 3;
    value.trace.loss_free = true;
    value.succeeded = true;
    value.code = "replay_evidence_streamed";
    qa::describe_linear_terminal(value, false);
    return value;
}

// The 1.4 payload with its schema changed and, below 1.4, without the 1.4 fields.
std::string payload(std::string_view schema) {
    const auto encoded = qa::encode_replay_execution_result(terminal(), 2U);
    const auto &bytes = std::get<std::vector<std::byte>>(encoded);
    auto document = toml::parse(
        std::string_view{reinterpret_cast<const char *>(bytes.data() + qa::protocol_header_bytes),
                         bytes.size() - qa::protocol_header_bytes});
    if (schema != "1.4")
        for (const auto *key : {"traversal", "playback", "linear_completed", "evidence_reasons",
                                "ended_paused", "user_pause_ms", "interruptions"})
            document.erase(key);
    if (schema == "1.2")
        document.erase("presentation");
    document.insert_or_assign("schema", std::string{schema});
    std::ostringstream text;
    text << document;
    return text.str();
}

void terminal_versions() {
    for (const auto *schema : {"1.2", "1.3", "1.4"}) {
        const auto decoded = qa::decode_replay_execution_result(message(payload(schema)), 2U);
        const auto *result = std::get_if<qa::ReplayExecutionResult>(&decoded);
        expect(result != nullptr && result->run_id == "run-77",
               std::string{"RNF-5: terminal "} + schema + " is read");
        if (result != nullptr)
            expect((std::string_view{schema} == "1.4") == result->traversal.has_value(),
                   "C2: before 1.4 the traversal is unknown, never linear");
    }
    for (const auto *schema : {"2.0", "1.5"})
        expect(std::holds_alternative<qa::ReplayExecutionResultError>(
                   qa::decode_replay_execution_result(message(payload(schema)), 2U)),
               std::string{"RNF-5: terminal "} + schema + " is rejected");
}

qa::RequestSummaryDocument summary(std::string run) {
    qa::RequestSummaryDocument value;
    value.request_id = "request-77";
    value.conditions_id = "conditions";
    value.run_id = std::move(run);
    value.takes = {{0,
                    "take.ayr",
                    std::string(64, 'a'),
                    "run",
                    "complete",
                    "replay_evidence_complete",
                    "natural_end",
                    "linear",
                    "complete",
                    {}}};
    return value;
}

// A cut before or after the flush, before the rename, keeps the confirmed version.
void cut_during_summary_write(const std::filesystem::path &root) {
    const auto path = root / "requests" / "run-77" / "request-summary.toml";
    expect(std::holds_alternative<qa::DurablePublishedFile>(
               qa::write_request_summary(path, summary("first"))),
           "RNF-5: the first summary is confirmed");
    for (const auto stop :
         {qa::DurablePublicationStop::before_flush, qa::DurablePublicationStop::after_flush}) {
        const auto cut = qa::write_request_summary(path, summary("second"), {std::nullopt, stop});
        expect(std::holds_alternative<qa::DurablePublishError>(cut),
               "RNF-5: the interrupted write reports the cut");
        const auto read = qa::read_request_summary(path);
        expect(std::holds_alternative<qa::RequestSummaryDocument>(read) &&
                   std::get<qa::RequestSummaryDocument>(read).run_id == "first",
               "RNF-5: after a cut the confirmed summary is still the one read");
    }
    std::size_t files{};
    for (const auto &entry : std::filesystem::directory_iterator{path.parent_path()}) {
        (void)entry;
        ++files;
    }
    expect(files == 1U, "RNF-5: a cut leaves no partial file next to the summary");
}

void cut_during_traversal_write(const std::filesystem::path &root) {
    const auto path = root / "runs" / "run-77" / "traversal.toml";
    qa::TraversalRecorder recorder{6};
    recorder.frame_played(0);
    recorder.frame_played(2);
    const auto first = recorder.document();
    expect(std::holds_alternative<qa::DurablePublishedFile>(qa::write_traversal(path, first)),
           "RNF-5: the first traversal is confirmed");
    recorder.frame_played(5);
    for (const auto stop :
         {qa::DurablePublicationStop::before_flush, qa::DurablePublicationStop::after_flush}) {
        expect(std::holds_alternative<qa::DurablePublishError>(
                   qa::write_traversal(path, recorder.document(), {std::nullopt, stop})),
               "RNF-5: the interrupted traversal write reports the cut");
        const auto read = qa::read_traversal(path);
        expect(std::holds_alternative<qa::TraversalDocument>(read) &&
                   std::get<qa::TraversalDocument>(read) == first,
               "RNF-5: after a cut the confirmed traversal is still the one read");
    }
}

void unknown_major_is_untouched(const std::filesystem::path &root) {
    const auto summary_path = root / "future" / "request-summary.toml";
    const auto traversal_path = root / "future" / "traversal.toml";
    std::filesystem::create_directories(summary_path.parent_path());
    const std::string future_summary = "schema_version = 9\nschema_minor = 0\nkind = 'x'\n";
    const std::string future_traversal = "schema_version = 9\nschema_minor = 0\n";
    {
        std::ofstream{summary_path, std::ios::binary} << future_summary;
        std::ofstream{traversal_path, std::ios::binary} << future_traversal;
    }
    expect(std::get<qa::RequestSummaryReadError>(qa::read_request_summary(summary_path)) ==
                   qa::RequestSummaryReadError::unsupported_version &&
               read_text(summary_path) == future_summary,
           "RNF-5: an unknown summary major is rejected without mutation");
    expect(std::get<qa::TraversalReadError>(qa::read_traversal(traversal_path)) ==
                   qa::TraversalReadError::unsupported_version &&
               read_text(traversal_path) == future_traversal,
           "RNF-5: an unknown traversal major is rejected without mutation");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-evidence-v11";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    terminal_versions();
    cut_during_summary_write(root);
    cut_during_traversal_write(root);
    unknown_major_is_untouched(root);
    std::filesystem::remove_all(root, ignored);
    if (failures != 0)
        return 1;
    std::cout << "evidence 1.1 is read, rejected and recovered safely\n";
    return 0;
}
