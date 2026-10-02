#include "qa_capabilities.h"

#include "version_info.h"

#if defined(AYTHER_AUDIO_QA_ENGINE_PACKAGE)
#include <ayther/engine/audio_observer.hpp>
#endif

#if defined(AYTHER_RUNTIME_AUDIO_QA)
#include "capabilities.h"
#include "contract_version.h"
#endif

#include <string>

namespace ayther::runtime {

#if defined(AYTHER_RUNTIME_AUDIO_QA)
namespace {

void append_version(std::string &report, const ayther::audio_qa::ContractVersion version) {
    report.append("[\"");
    report.append(std::to_string(version.major));
    report.push_back('.');
    report.append(std::to_string(version.minor));
    report.append("\"]");
}

} // namespace
#endif

std::string qa_capabilities_report() {
    std::string report{R"({"schema":"1.0","runtime_version":")"};
    report.append(runtime_version);
    report.append(R"(","engine_version":")");
    report.append(linked_engine_version());
    report.append(R"(","contracts":{)");
#if defined(AYTHER_AUDIO_QA_ENGINE_PACKAGE)
    const auto observation = ayther::engine::audio_observation::contract_version;
    report.append(R"("engine_observation":")");
    report.append(std::to_string(observation.major));
    report.push_back('.');
    report.append(std::to_string(observation.minor));
    report.push_back('"');
#endif
#if defined(AYTHER_RUNTIME_AUDIO_QA)
    report.clear();
    report.append(R"({"schema":"1.0","runtime_version":")");
    report.append(runtime_version);
    report.append(R"(","engine_version":")");
    report.append(linked_engine_version());
    report.append(R"(","contracts":{"engine":)");
    append_version(report, ayther::audio_qa::engine_contract_version);
    report.append(R"(,"runtime":)");
    append_version(report, ayther::audio_qa::runtime_protocol_version);
    report.append(R"(,"evidence":)");
    append_version(report, ayther::audio_qa::evidence_schema_version);
    report.append(R"(,"hd_state":)");
    append_version(report, ayther::audio_qa::hd_state_schema_version);
    report.append(R"(},"capabilities":[)");
    for (std::size_t index{}; index < ayther::audio_qa::required_capabilities.size(); ++index) {
        if (index != 0U) {
            report.push_back(',');
        }
        report.push_back('"');
        report.append(ayther::audio_qa::required_capabilities[index]);
        report.push_back('"');
    }
    const auto &limits = ayther::audio_qa::required_limits;
    report.append(R"(,"visible_replay_v1")");
    report.append(R"(],"limits":{"fact_bytes":)");
    report.append(std::to_string(limits.fact_bytes));
    report.append(R"(,"batch_bytes":)");
    report.append(std::to_string(limits.batch_bytes));
    report.append(R"(,"live_occurrences":)");
    report.append(std::to_string(limits.live_occurrences));
    report.append(R"(,"audio_channels":)");
    report.append(std::to_string(limits.audio_channels));
    report.append(R"(,"sample_rate":)");
    report.append(std::to_string(limits.sample_rate));
    report.append(R"(,"cancel_milliseconds":)");
    report.append(std::to_string(limits.cancel_milliseconds));
    report.append("}}");
#else
    report.append(R"(},"capabilities":[]})");
#endif
    return report;
}

} // namespace ayther::runtime
