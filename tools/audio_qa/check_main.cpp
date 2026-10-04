#include "campaign_audit.h"
#include "check_messages.h"
#include "check_option_descriptors.h"
#include "check_options.h"
#include "check_runner.h"
#include "console_interrupt.h"
#include "evidence_query_index.h"
#include "query_options.h"

#if defined(AYTHER_AUDIO_QA_HAS_RUNTIME_VERSION)
#include "ayther_runtime_version.h"
#endif

#include <charconv>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr int incomplete_exit_code = 2;
constexpr int invalid_invocation_exit_code = 3;
constexpr int evidence_failure_exit_code = 4;

void emit_user_message(ayther::audio_qa::CheckLanguage language,
                       ayther::audio_qa::CheckMessage message) {
    std::cerr << "audio_qa_message[" << ayther::audio_qa::check_language_code(language)
              << "]: " << ayther::audio_qa::check_message(language, message) << '\n';
}

// Spec 002 (RF-1.2, RNF-7): parse errors happen before the language is validated;
// honour `--language en` when it is present so the message is still localized.
ayther::audio_qa::CheckLanguage language_hint(std::span<const std::string_view> arguments) {
    for (std::size_t index = 0; index + 1U < arguments.size(); ++index)
        if (arguments[index] == "--language" && arguments[index + 1U] == "en")
            return ayther::audio_qa::CheckLanguage::english;
    return ayther::audio_qa::CheckLanguage::spanish;
}

int run_query(std::span<const std::string_view> arguments) {
    const auto parsed = ayther::audio_qa::parse_query_options(arguments);
    const auto *options = std::get_if<ayther::audio_qa::QueryOptions>(&parsed);
    if (options == nullptr) {
        const auto &error = std::get<ayther::audio_qa::CheckOptionError>(parsed);
        std::cerr << "audio_qa_error: " << ayther::audio_qa::check_option_error_code(error.code);
        if (!error.option.empty())
            std::cerr << ": " << error.option;
        std::cerr << '\n';
        emit_user_message(ayther::audio_qa::CheckLanguage::spanish,
                          ayther::audio_qa::CheckMessage::invalid_options);
        return invalid_invocation_exit_code;
    }
    const auto language = options->language == "en" ? ayther::audio_qa::CheckLanguage::english
                                                    : ayther::audio_qa::CheckLanguage::spanish;
    const auto opened = ayther::audio_qa::read_evidence_query_index(options->index);
    const auto *index = std::get_if<ayther::audio_qa::EvidenceQueryIndex>(&opened);
    if (index == nullptr) {
        std::cerr << "audio_qa_error: evidence_query_index_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::query_index_unavailable);
        return evidence_failure_exit_code;
    }
    const ayther::audio_qa::FactId origin{options->run_id, options->producer_id, options->sequence};
    const auto routes = ayther::audio_qa::query_evidence_index(*index, origin);
    std::cout << ayther::audio_qa::format_evidence_query(origin, routes) << '\n';
    if (routes.empty()) {
        emit_user_message(language, ayther::audio_qa::CheckMessage::query_not_found);
        return incomplete_exit_code;
    }
    emit_user_message(language, ayther::audio_qa::CheckMessage::query_found);
    return 0;
}

int run_audit(const std::span<const std::string_view> arguments) {
    std::string run_directory;
    std::string run_id;
    std::string expected_text;
    std::string language{"es"};
    for (std::size_t index{}; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (index + 1U >= arguments.size()) {
            std::cerr << "audio_qa_error: invalid_audit_options\n";
            return invalid_invocation_exit_code;
        }
        const auto value = arguments[++index];
        std::string *destination{};
        if (option == "--run-directory")
            destination = &run_directory;
        else if (option == "--run-id")
            destination = &run_id;
        else if (option == "--expected-assignments")
            destination = &expected_text;
        else if (option == "--language")
            destination = &language;
        else {
            std::cerr << "audio_qa_error: invalid_audit_option: " << option << '\n';
            return invalid_invocation_exit_code;
        }
        if (!destination->empty() && destination != &language) {
            std::cerr << "audio_qa_error: duplicate_audit_option: " << option << '\n';
            return invalid_invocation_exit_code;
        }
        destination->assign(value);
    }
    std::uint64_t expected{};
    const auto parsed = std::from_chars(expected_text.data(),
                                        expected_text.data() + expected_text.size(), expected);
    if (run_directory.empty() || run_id.empty() || expected_text.empty() ||
        parsed.ec != std::errc{} || parsed.ptr != expected_text.data() + expected_text.size() ||
        (language != "es" && language != "en")) {
        std::cerr << "audio_qa_error: invalid_audit_options\n";
        return invalid_invocation_exit_code;
    }
    const auto audited = ayther::audio_qa::audit_campaign_evidence(
        std::filesystem::path{run_directory}, run_id, expected);
    const auto *summary = std::get_if<ayther::audio_qa::CampaignAuditSummary>(&audited);
    if (summary == nullptr) {
        std::cerr << "audio_qa_error: campaign_audit_"
                  << ayther::audio_qa::campaign_audit_error_code(
                         std::get<ayther::audio_qa::CampaignAuditError>(audited))
                  << '\n';
        std::cerr << "audio_qa_message[" << language << "]: "
                  << (language == "es" ? "No se pudo reabrir y auditar la evidencia."
                                       : "The evidence could not be reopened and audited.")
                  << '\n';
        return evidence_failure_exit_code;
    }
    std::cout << ayther::audio_qa::format_campaign_audit(*summary) << '\n';
    std::cerr << "audio_qa_message[" << language << "]: "
              << (summary->complete
                      ? (language == "es" ? "La evidencia de la campaña está completa."
                                          : "The campaign evidence is complete.")
                      : (language == "es" ? "La evidencia de la campaña está incompleta."
                                          : "The campaign evidence is incomplete."))
              << '\n';
    return summary->complete ? 0 : incomplete_exit_code;
}

// Spec 002 (RF-1.8, contracts.md C5): `options --format toml` publishes the
// inventory of the compiled version with the schema of the reference inventory.
int run_options(const std::span<const std::string_view> arguments) {
    if (arguments.size() != 2U || arguments[0] != "--format" || arguments[1] != "toml") {
        std::cerr << "audio_qa_error: invalid_options_invocation\n";
        std::cerr << ayther::audio_qa::format_check_usage() << '\n';
        return invalid_invocation_exit_code;
    }
#if defined(AYTHER_AUDIO_QA_HAS_RUNTIME_VERSION)
    constexpr std::string_view compiled_ref = ayther::runtime::runtime_version;
#else
    constexpr std::string_view compiled_ref = "unknown";
#endif
    std::cout << ayther::audio_qa::format_check_option_inventory(compiled_ref, "");
    return 0;
}

// The console client of run_check: every report line goes to stderr, as before, and the
// messages follow --language (RNF-7).
class ConsoleObserver final : public ayther::audio_qa::CheckObserver {
  public:
    explicit ConsoleObserver(ayther::audio_qa::CheckLanguage language) : language_(language) {}

    void on_phase(ayther::audio_qa::RequestPhase) override {}
    void on_replay_state(const ayther::audio_qa::ReplayStateView &) override {}
    void on_take_outcome(const ayther::audio_qa::TakeOutcome &) override {}
    void on_report(std::string_view line) override { std::cerr << line << '\n'; }
    void on_message(ayther::audio_qa::CheckMessage message) override {
        emit_user_message(language_, message);
    }

  private:
    ayther::audio_qa::CheckLanguage language_;
};

} // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && std::string_view{argv[1]} == "audit") {
        std::vector<std::string_view> audit_arguments;
        audit_arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index)
            audit_arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        return run_audit(audit_arguments);
    }
    if (argc >= 2 && std::string_view{argv[1]} == "query") {
        std::vector<std::string_view> query_arguments;
        query_arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index)
            query_arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        return run_query(query_arguments);
    }
    if (argc >= 2 && std::string_view{argv[1]} == "options") {
        std::vector<std::string_view> option_arguments;
        option_arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index)
            option_arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        return run_options(option_arguments);
    }
    if (argc < 2 || std::string_view{argv[1]} != "check") {
        std::cerr << "audio_qa_error: invalid_invocation: expected command 'check'\n";
        std::cerr << ayther::audio_qa::format_check_usage() << '\n';
        emit_user_message(ayther::audio_qa::CheckLanguage::spanish,
                          ayther::audio_qa::CheckMessage::invalid_invocation);
        return invalid_invocation_exit_code;
    }

    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 2));
    for (int index = 2; index < argc; ++index)
        arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");

    const auto parsed = ayther::audio_qa::parse_check_options(arguments);
    if (const auto *error = parsed.error()) {
        const auto code = ayther::audio_qa::check_option_error_code(error->code);
        std::cerr << "audio_qa_error: " << code;
        if (!error->option.empty())
            std::cerr << ": " << error->option;
        std::cerr << '\n';
        // RNF-7: the field and its problem, localized.
        const auto hinted = language_hint(arguments);
        if (const auto text = ayther::audio_qa::check_issue_message(hinted, code))
            std::cerr << "audio_qa_message[" << ayther::audio_qa::check_language_code(hinted)
                      << "]: " << (error->option.empty() ? "" : error->option + ": ") << *text
                      << '\n';
        emit_user_message(hinted, ayther::audio_qa::CheckMessage::invalid_options);
        return invalid_invocation_exit_code;
    }

    const auto language = parsed.options()->language == "en"
                              ? ayther::audio_qa::CheckLanguage::english
                              : ayther::audio_qa::CheckLanguage::spanish;

    // Spec 002 (contracts.md C5, BR-070): the command is a thin client of run_check; Ctrl+C
    // requests the same cancellation as the library token (RF-2.5).
    ayther::audio_qa::CancelToken cancel;
    const ayther::audio_qa::ConsoleInterruptHandler interrupt_handler{cancel};
    if (!interrupt_handler.installed()) {
        std::cerr << "audio_qa_error: interrupt_handler_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::interrupt_handler_unavailable);
        return evidence_failure_exit_code;
    }
    ConsoleObserver observer{language};
    const auto outcome = ayther::audio_qa::run_check(*parsed.options(), observer, cancel);
    return outcome.exit_code;
}
