#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace ayther::audio_qa {

enum class CheckLanguage { spanish, english };

enum class CheckMessage {
    invalid_invocation,
    invalid_options,
    missing_profile_primary_take,
    output_root_unavailable,
    request_ledger_unavailable,
    request_ledger_inconsistent,
    request_ledger_publish_failed,
    request_accepted,
    request_known,
    request_identity_conflict,
    session_busy,
    invalid_request,
    request_capacity_exceeded,
    interrupt_handler_unavailable,
    check_execution_unavailable,
    technical_summary_invalid,
    query_index_unavailable,
    query_found,
    query_not_found,
    // Spec 002 (RNF-7): the closing message of a request.
    summary_complete,
    summary_incomplete,
    summary_cancelled,
    summary_preservation_failure,
};

[[nodiscard]] std::optional<CheckLanguage> parse_check_language(std::string_view value) noexcept;
[[nodiscard]] std::string_view check_language_code(CheckLanguage language) noexcept;
[[nodiscard]] std::string_view check_message(CheckLanguage language, CheckMessage message) noexcept;
[[nodiscard]] std::span<const CheckMessage> all_check_messages() noexcept;
// Spec 002 (RNF-7): the localized text of a code written in `audio_qa_error` or as a take
// diagnostic; nullopt for a code without text.
[[nodiscard]] std::optional<std::string_view> check_issue_message(CheckLanguage language,
                                                                  std::string_view code) noexcept;

} // namespace ayther::audio_qa
