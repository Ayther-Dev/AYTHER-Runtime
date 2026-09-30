#include "check_messages.h"
#include "check_options.h"

#include <array>

namespace qa = ayther::audio_qa;

int main() {
    constexpr std::array messages{
        qa::CheckMessage::invalid_invocation,
        qa::CheckMessage::invalid_options,
        qa::CheckMessage::missing_profile_primary_take,
        qa::CheckMessage::output_root_unavailable,
        qa::CheckMessage::request_ledger_unavailable,
        qa::CheckMessage::request_ledger_inconsistent,
        qa::CheckMessage::request_ledger_publish_failed,
        qa::CheckMessage::request_accepted,
        qa::CheckMessage::request_known,
        qa::CheckMessage::request_identity_conflict,
        qa::CheckMessage::session_busy,
        qa::CheckMessage::invalid_request,
        qa::CheckMessage::request_capacity_exceeded,
        qa::CheckMessage::interrupt_handler_unavailable,
        qa::CheckMessage::check_execution_unavailable,
        qa::CheckMessage::technical_summary_invalid,
        qa::CheckMessage::query_index_unavailable,
        qa::CheckMessage::query_found,
        qa::CheckMessage::query_not_found,
    };
    for (const auto message : messages) {
        const auto spanish = qa::check_message(qa::CheckLanguage::spanish, message);
        const auto english = qa::check_message(qa::CheckLanguage::english, message);
        if (spanish.empty() || english.empty() || spanish == english)
            return 1;
    }
    const qa::CheckOptions defaults;
    return defaults.language == "es" &&
                   qa::parse_check_language(defaults.language) == qa::CheckLanguage::spanish &&
                   qa::parse_check_language("en") == qa::CheckLanguage::english &&
                   !qa::parse_check_language("fr") &&
                   qa::check_language_code(qa::CheckLanguage::spanish) == "es" &&
                   qa::check_language_code(qa::CheckLanguage::english) == "en"
               ? 0
               : 1;
}
