#include "evidence_query_index.h"

#include <array>
#include <string>

namespace qa = ayther::audio_qa;

int main() {
    const qa::FactId event{"run-query", "detector", 1};
    const qa::FactId selection{"run-query", "selection", 2};
    qa::EventAudioRoute route;
    route.basis = qa::TraceRelationBasis::explicit_cause;
    route.fact_path = {event, selection};
    route.mix_span_id = "mix-17";
    route.output_range = {"captured-output", 48000, 100, 132};
    route.relation_reason = "cause_ids";
    qa::EventAudioQueryResult query;
    query.status = qa::EventAudioQueryStatus::found;
    query.routes.push_back(route);

    const auto built = qa::make_evidence_query_index(event, query);
    const auto *index = std::get_if<qa::EvidenceQueryIndex>(&built);
    if (index == nullptr)
        return 1;
    const auto encoded = qa::evidence_query_index_to_toml(*index);
    const auto *text = std::get_if<std::string>(&encoded);
    if (text == nullptr)
        return 2;
    const auto decoded = qa::evidence_query_index_from_toml(*text);
    const auto *reopened = std::get_if<qa::EvidenceQueryIndex>(&decoded);
    if (reopened == nullptr || *reopened != *index)
        return 3;
    const auto routes = qa::query_evidence_index(*reopened, event);
    const auto report = qa::format_evidence_query(event, routes);
    constexpr std::array forbidden{"bug_status", "confirmed", "score",   "resolved",
                                   "confirmado", "puntua",    "resuelto"};
    if (routes != std::vector<qa::EventAudioRoute>{route} ||
        report.find("event=run-query/detector/1") == std::string::npos ||
        report.find("audio=captured-output@48000[100,132)") == std::string::npos)
        return 4;
    for (const auto *word : forbidden)
        if (report.find(word) != std::string::npos)
            return 5;
    const qa::FactId absent{"run-query", "detector", 99};
    return qa::query_evidence_index(*reopened, absent).empty() &&
                   qa::format_evidence_query(absent, {}).find("status=not_found") !=
                       std::string::npos
               ? 0
               : 6;
}
