// Spec 002, BR-073 (RF-2.3, RF-5.8; contracts.md C1): the supervisor side of the Runtime
// QA protocol 1.1, with synthetic messages. Visible replay negotiates 1.1 and requires
// inspection_v1 and visible_replay_v1; without presentation a 1.0 Runtime keeps working.
// `session_status` brings the live replay state and the opening of a post-end inspection,
// and `inspection_event` facts are read with their typed fields.
#include "control_message.h"
#include "inspection_fact_builder.h"
#include "inspection_facts.h"
#include "protocol_header.h"
#include "runtime_protocol_v11.h"
#include "session_status_message.h"

#include <iostream>
#include <string>
#include <string_view>
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

qa::CapabilitySet offer(std::vector<qa::ContractVersion> runtime, std::vector<std::string> extra) {
    qa::CapabilitySet set;
    set.contracts[0] = {qa::engine_contract_version};
    set.contracts[1] = std::move(runtime);
    set.contracts[2] = {qa::evidence_schema_version};
    set.contracts[3] = {qa::hd_state_schema_version};
    for (const auto capability : qa::required_capabilities)
        set.capabilities.emplace_back(capability);
    for (auto &capability : extra)
        set.capabilities.push_back(std::move(capability));
    set.limits = qa::required_limits;
    return set;
}

std::vector<std::byte> session_status(std::string_view toml, std::uint64_t sequence = 3U) {
    const auto header = qa::encode_protocol_header(
        {qa::MessageType::session_status, static_cast<std::uint32_t>(toml.size()), 2U, sequence});
    std::vector<std::byte> bytes(header.begin(), header.end());
    for (const char character : toml)
        bytes.push_back(static_cast<std::byte>(character));
    return bytes;
}

void negotiation() {
    const qa::ContractVersion v10{1, 0};
    const qa::ContractVersion v11{1, 1};
    const auto full = offer({v10, v11}, {"visible_replay_v1", "inspection_v1"});
    const auto visible = qa::negotiate_runtime_protocol(full, "visible");
    expect(std::holds_alternative<qa::ContractVersion>(visible) &&
               std::get<qa::ContractVersion>(visible) == v11,
           "C1: visible replay negotiates protocol 1.1");
    const auto without_inspection =
        qa::negotiate_runtime_protocol(offer({v10, v11}, {"visible_replay_v1"}), "visible");
    expect(std::holds_alternative<qa::FieldIssue>(without_inspection) &&
               std::get<qa::FieldIssue>(without_inspection) ==
                   qa::FieldIssue{"--runtime", "runtime_missing_inspection_v1"},
           "C1: visible replay without inspection_v1 is rejected naming it");
    const auto legacy_visible = qa::negotiate_runtime_protocol(
        offer({v10}, {"visible_replay_v1", "inspection_v1"}), "visible");
    expect(std::holds_alternative<qa::FieldIssue>(legacy_visible) &&
               std::get<qa::FieldIssue>(legacy_visible).code == "runtime_missing_protocol_1_1",
           "C1: visible replay with a 1.0-only Runtime is rejected");
    const auto legacy_none = qa::negotiate_runtime_protocol(offer({v10}, {}), "none");
    expect(std::holds_alternative<qa::ContractVersion>(legacy_none) &&
               std::get<qa::ContractVersion>(legacy_none) == v10,
           "RNF-6: without presentation a 1.0 Runtime keeps working");
    const auto modern_none = qa::negotiate_runtime_protocol(full, "none");
    expect(std::holds_alternative<qa::ContractVersion>(modern_none) &&
               std::get<qa::ContractVersion>(modern_none) == v11,
           "C1: without presentation 1.1 is chosen when offered");
}

void replay_state() {
    const auto decoded = qa::decode_session_status(
        session_status("status = \"replay_state\"\nrun_id = \"run-1-take-2\"\ntake_position = 1\n"
                       "phase = \"paused\"\nframe = 4310\nframes_total = 7892\n"
                       "overlay_visible = true\n"),
        3U);
    const auto *state = std::get_if<qa::ReplayStateView>(&decoded);
    expect(state != nullptr && state->run_id == "run-1-take-2" && state->take_position == 1U &&
               state->phase == "paused" && state->frame == 4310U && state->frames_total == 7892U &&
               state->overlay_visible && state->interruption_cause.empty(),
           "RF-2.3: the live replay state is read");
    const auto before_first = qa::decode_session_status(
        session_status("status = \"replay_state\"\nrun_id = \"r\"\ntake_position = 0\n"
                       "phase = \"preparing\"\nframes_total = 6\noverlay_visible = false\n"),
        3U);
    expect(std::holds_alternative<qa::ReplayStateView>(before_first) &&
               !std::get<qa::ReplayStateView>(before_first).frame,
           "C1-3: before the first frame there is no frame");
    const auto interrupted = qa::decode_session_status(
        session_status("status = \"replay_state\"\nrun_id = \"r\"\ntake_position = 0\n"
                       "phase = \"interrupted\"\nframe = 3\nframes_total = 6\n"
                       "overlay_visible = false\ninterruption_cause = \"video_acquire_failed\"\n"),
        3U);
    expect(std::holds_alternative<qa::ReplayStateView>(interrupted) &&
               std::get<qa::ReplayStateView>(interrupted).interruption_cause ==
                   "video_acquire_failed",
           "RF-2.6: an interruption keeps its cause");
    for (const auto *invalid :
         {"status = \"replay_state\"\nrun_id = \"r\"\ntake_position = 0\nphase = \"dancing\"\n"
          "frames_total = 6\noverlay_visible = false\n",
          "status = \"replay_state\"\nrun_id = \"r\"\ntake_position = 0\nphase = \"paused\"\n"
          "frame = 9\nframes_total = 6\noverlay_visible = false\n",
          "status = \"replay_state\"\nrun_id = \"r\"\ntake_position = 0\nphase = \"playing\"\n"
          "frames_total = 6\noverlay_visible = false\ninterruption_cause = \"x\"\n",
          "status = \"teleport\"\n"})
        expect(std::holds_alternative<qa::ProtocolV11Error>(
                   qa::decode_session_status(session_status(invalid), 3U)),
               std::string{"C1: an invalid status is rejected: "} + invalid);
    expect(std::holds_alternative<qa::ProtocolV11Error>(
               qa::decode_session_status(session_status("status = \"replay_state\"\n", 3U), 4U)),
           "C1: the sequence is checked");
}

void run_opened() {
    const auto decoded = qa::decode_session_status(
        session_status("status = \"run_opened\"\nrun_id = \"run-1-take-2-inspection-1\"\n"
                       "take_position = 1\ntraversal = \"post_end_inspection\"\n"),
        3U);
    const auto *opened = std::get_if<qa::RunOpened>(&decoded);
    expect(opened != nullptr && opened->run_id == "run-1-take-2-inspection-1" &&
               opened->take_position == 1U,
           "RF-2.9: a post-end inspection opens its own run");
    expect(std::holds_alternative<qa::ProtocolV11Error>(qa::decode_session_status(
               session_status("status = \"run_opened\"\nrun_id = \"r\"\ntake_position = 1\n"
                              "traversal = \"linear\"\n"),
               3U)),
           "RF-2.9: only a post-end inspection opens a run");
}

qa::FactField field(std::string name, qa::FactFieldValue value) {
    return {
        std::move(name), qa::Availability::known, qa::FactFieldUnit::none, std::move(value), {}};
}

void inspection_event() {
    qa::Fact fact;
    fact.id = {"run-1", "inspection", 2};
    fact.kind = "inspection_event";
    fact.fields = {field("seq", std::uint64_t{2}),
                   field("control", std::string{"step_back"}),
                   field("frame_before", std::uint64_t{4310}),
                   field("frame_after", std::uint64_t{4309}),
                   field("visit", std::uint64_t{2}),
                   field("elapsed_ms", std::uint64_t{71850})};
    const auto event = qa::read_inspection_event(fact);
    expect(event && event->seq == 2U && event->control == "step_back" &&
               event->frame_before == 4310U && event->frame_after == 4309U && event->visit == 2U &&
               event->elapsed_ms == 71850U,
           "RF-5.8: an inspection event is read with its typed fields");
    auto unknown_control = fact;
    unknown_control.fields[1] = field("control", std::string{"teleport"});
    auto missing = fact;
    missing.fields.pop_back();
    auto other = fact;
    other.kind = "playback_effect";
    expect(!qa::read_inspection_event(unknown_control) && !qa::read_inspection_event(missing) &&
               !qa::read_inspection_event(other),
           "C2: an unknown control, a missing field or another class is not an event");
}

// BR-135: the Runtime side encodes what the supervisor reads.
void runtime_side() {
    qa::ReplayStateMessage state{"run-1-take-2", 2, "paused", 4310U, 7892U, true, {}};
    const auto encoded = qa::encode_replay_state(state, 5U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    const auto decoded = bytes == nullptr ? qa::SessionStatus{qa::ProtocolV11Error::header_rejected}
                                          : qa::decode_session_status(*bytes, 5U);
    const auto *view = std::get_if<qa::ReplayStateView>(&decoded);
    expect(view != nullptr && view->run_id == "run-1-take-2" && view->take_position == 2U &&
               view->phase == "paused" && view->frame == 4310U && view->frames_total == 7892U &&
               view->overlay_visible,
           "C1-3: the Runtime encodes the replay state the supervisor reads");
    state.phase = "interrupted";
    state.interruption_cause = "video_acquire_failed";
    state.frame.reset();
    const auto interrupted = qa::encode_replay_state(state, 6U);
    const auto *interrupted_bytes = std::get_if<std::vector<std::byte>>(&interrupted);
    expect(interrupted_bytes != nullptr &&
               std::get<qa::ReplayStateView>(qa::decode_session_status(*interrupted_bytes, 6U))
                       .interruption_cause == "video_acquire_failed",
           "C1-3: an interruption is encoded with its cause");
    state.phase = "dancing";
    expect(std::holds_alternative<qa::SessionStatusEncodeError>(qa::encode_replay_state(state, 7U)),
           "C1: an unknown phase is never sent");
    const auto opened = qa::encode_run_opened({"run-1-take-2-inspection-1", 2}, 8U);
    const auto *opened_bytes = std::get_if<std::vector<std::byte>>(&opened);
    expect(opened_bytes != nullptr &&
               std::get<qa::RunOpened>(qa::decode_session_status(*opened_bytes, 8U)) ==
                   qa::RunOpened{"run-1-take-2-inspection-1", 2},
           "C1-6: the Runtime announces a post-end inspection run");

    const qa::InspectionEvent event{3, "step_back", 41, 40, 2, 71900};
    const auto fact = qa::make_inspection_event_fact("run-1-take-2", 3, event);
    expect(qa::well_formed(fact) && qa::read_inspection_event(fact) == event,
           "C2: the Runtime builds inspection events with their typed fields");

    // BR-146, BR-147 (C2, RF-7.4, RF-7.7): the record of a visit, with its measures when known.
    const auto record = qa::make_render_frame_fact("run-1-take-2", 4, {40, 3, {}, 12, 1.25, {}});
    const auto read = qa::read_render_frame(record);
    expect(qa::well_formed(record) && read && read->frame == 40U && read->visit == 3U &&
               read->composable && read->occurrences == 12U && read->processing_ms == 1.25 &&
               !read->fps_instant,
           "C2: a render_frame keeps frame, visit, occurrences and only the known measures");
    const auto split =
        qa::make_render_frame_fact("run-1-take-2", 5, {41, 1, "raster_split", 0, {}, 59.9});
    const auto split_read = qa::read_render_frame(split);
    expect(split_read && !split_read->composable &&
               split_read->not_composable_reason == "raster_split" && split_read->fps_instant,
           "C2: a frame that cannot be composed names its reason");
}

// C1-2: the request carries its language in 1.1; a 1.0 request does not.
void request_language() {
    qa::Request request{
        "req-1", "session-1", "conditions-1", {"a.ayr"}, qa::Admission::pending, std::string{"en"}};
    const auto encoded = qa::encode_request_message(request, 1U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    const auto decoded = bytes == nullptr
                             ? qa::DecodedControlMessage{qa::ControlMessageError::header_rejected}
                             : qa::decode_request_message(*bytes, 1U);
    const auto *value = std::get_if<qa::Request>(&decoded);
    expect(value != nullptr && value->language == std::optional<std::string>{"en"},
           "C1-2: the language of a 1.1 request travels with it");
    request.language.reset();
    const auto legacy = qa::encode_request_message(request, 1U);
    const auto *legacy_bytes = std::get_if<std::vector<std::byte>>(&legacy);
    const auto legacy_decoded =
        legacy_bytes == nullptr
            ? qa::DecodedControlMessage{qa::ControlMessageError::header_rejected}
            : qa::decode_request_message(*legacy_bytes, 1U);
    expect(std::holds_alternative<qa::Request>(legacy_decoded) &&
               !std::get<qa::Request>(legacy_decoded).language,
           "RNF-6: a 1.0 request has no language");
    request.language = "fr";
    expect(!std::holds_alternative<std::vector<std::byte>>(qa::encode_request_message(request, 1U)),
           "C1-2: only es or en");
}

} // namespace

int main() {
    negotiation();
    replay_state();
    run_opened();
    inspection_event();
    runtime_side();
    request_language();
    if (failures != 0)
        return 1;
    std::cout << "protocol 1.1 is read on the supervisor side\n";
    return 0;
}
