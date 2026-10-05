#include "inspection_evidence.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <system_error>

namespace ayther::audio_qa {
namespace {

constexpr std::int64_t traversal_schema_version = 1;
constexpr std::int64_t traversal_schema_minor = 1;
constexpr std::uintmax_t max_traversal_bytes = 64U * 1024U * 1024U;

bool moves_position(std::string_view control) {
    return control == "step_forward" || control == "step_back";
}

std::optional<std::uint64_t> unsigned_value(const toml::node_view<const toml::node> node) {
    const auto value = node.value<std::int64_t>();
    if (!value || *value < 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(*value);
}

const toml::array *tables(const toml::table &document, std::string_view key) {
    const auto *array = document[key].as_array();
    if (array == nullptr || !std::all_of(array->begin(), array->end(),
                                         [](const toml::node &node) { return node.is_table(); }))
        return nullptr;
    return array;
}

} // namespace

TraversalRecorder::TraversalRecorder(std::uint64_t frames_total) noexcept {
    document_.frames_total = frames_total;
}

void TraversalRecorder::frame_played(std::uint64_t frame) {
    last_frame_ = frame;
    if (after_visit_ && !document_.visits.empty()) {
        // Playback after an inspection continues from the visit that ended it.
        auto &resumes = document_.resume_after;
        if (resumes.empty() || resumes.back().seq != document_.visits.back().seq)
            resumes.push_back({document_.visits.back().seq, frame, frame, "continued"});
        else
            resumes.back().to = frame;
        return;
    }
    auto &segments = document_.segments;
    if (segments.empty())
        segments.push_back({frame, frame, "linear"});
    else
        segments.back().to = frame;
}

void TraversalRecorder::inspection(const InspectionEvent &event) {
    if (event.control == "interrupted") {
        document_.interruptions.push_back({event.seq, event.frame_before});
        return;
    }
    document_.visits.push_back({event.seq, event.frame_after, event.control});
    after_visit_ = true;
    if (moves_position(event.control))
        document_.kind = TraversalKind::inspection;
}

TraversalDocument TraversalRecorder::document() const {
    auto document = document_;
    document.linear_completed = document.kind == TraversalKind::linear && last_frame_ &&
                                document.frames_total > 0U &&
                                *last_frame_ + 1U == document.frames_total;
    return document;
}

TraversalDocument traversal_of_take(std::uint64_t frames_total, std::uint64_t frames_consumed,
                                    std::span<const InspectionEvent> events) {
    TraversalRecorder recorder{frames_total};
    if (frames_consumed == 0U) {
        for (const auto &event : events)
            recorder.inspection(event);
        return recorder.document();
    }
    const auto last = frames_consumed - 1U;
    if (events.empty()) {
        recorder.frame_played(0U);
        recorder.frame_played(last);
        return recorder.document();
    }
    const auto first_event = events.front().frame_before;
    recorder.frame_played(0U);
    recorder.frame_played(std::min(first_event, last));
    // D-5 (C2, RF-2.12): the frames played between an event and the next one are a stretch of
    // their own, after every resume and not only after the last one. Playback continues from the
    // frame after the event up to the frame where the next event happened, or up to the last
    // frame consumed after the last event.
    for (std::size_t index = 0; index < events.size(); ++index) {
        recorder.inspection(events[index]);
        const auto resumed = events[index].frame_after + 1U;
        const auto until =
            index + 1U < events.size() ? std::min(events[index + 1U].frame_before, last) : last;
        if (resumed <= until) {
            recorder.frame_played(resumed);
            recorder.frame_played(until);
        }
    }
    return recorder.document();
}

std::string format_traversal(const TraversalDocument &document) {
    toml::array segments;
    for (const auto &segment : document.segments)
        segments.push_back(toml::table{{"from", static_cast<std::int64_t>(segment.from)},
                                       {"to", static_cast<std::int64_t>(segment.to)},
                                       {"mode", segment.mode}});
    toml::array visits;
    for (const auto &visit : document.visits)
        visits.push_back(toml::table{{"seq", static_cast<std::int64_t>(visit.seq)},
                                     {"frame", static_cast<std::int64_t>(visit.frame)},
                                     {"control", visit.control}});
    toml::array resumes;
    for (const auto &resume : document.resume_after)
        resumes.push_back(toml::table{{"seq", static_cast<std::int64_t>(resume.seq)},
                                      {"from_frame", static_cast<std::int64_t>(resume.from_frame)},
                                      {"to", static_cast<std::int64_t>(resume.to)},
                                      {"mode", resume.mode}});
    toml::array interruptions;
    for (const auto &interruption : document.interruptions)
        interruptions.push_back(
            toml::table{{"seq", static_cast<std::int64_t>(interruption.seq)},
                        {"frame", static_cast<std::int64_t>(interruption.frame)}});
    const toml::table table{{"schema_version", traversal_schema_version},
                            {"schema_minor", traversal_schema_minor},
                            {"kind", std::string{traversal_kind_code(document.kind)}},
                            {"linear_completed", document.linear_completed},
                            {"frames_total", static_cast<std::int64_t>(document.frames_total)},
                            {"segments", std::move(segments)},
                            {"visits", std::move(visits)},
                            {"resume_after", std::move(resumes)},
                            {"interruptions", std::move(interruptions)}};
    std::ostringstream output;
    output << table << '\n';
    return output.str();
}

TraversalReadResult parse_traversal(std::string_view text) {
    try {
        const auto table = toml::parse(text);
        const auto version = table["schema_version"].value<std::int64_t>();
        if (!version)
            return TraversalReadError::invalid;
        if (*version != traversal_schema_version)
            return TraversalReadError::unsupported_version;
        TraversalDocument document;
        const auto kind = table["kind"].value<std::string>();
        const auto parsed_kind = kind ? parse_traversal_kind(*kind) : std::nullopt;
        const auto completed = table["linear_completed"].value<bool>();
        const auto total = unsigned_value(table["frames_total"]);
        const auto *segments = tables(table, "segments");
        const auto *visits = tables(table, "visits");
        const auto *resumes = tables(table, "resume_after");
        const auto *interruptions = tables(table, "interruptions");
        if (!parsed_kind || !completed || !total || segments == nullptr || visits == nullptr ||
            resumes == nullptr || interruptions == nullptr)
            return TraversalReadError::invalid;
        document.kind = *parsed_kind;
        document.linear_completed = *completed;
        document.frames_total = *total;
        for (const auto &node : *segments) {
            const auto &entry = *node.as_table();
            const auto from = unsigned_value(entry["from"]);
            const auto to = unsigned_value(entry["to"]);
            const auto mode = entry["mode"].value<std::string>();
            if (!from || !to || !mode)
                return TraversalReadError::invalid;
            document.segments.push_back({*from, *to, *mode});
        }
        for (const auto &node : *visits) {
            const auto &entry = *node.as_table();
            const auto seq = unsigned_value(entry["seq"]);
            const auto frame = unsigned_value(entry["frame"]);
            const auto control = entry["control"].value<std::string>();
            if (!seq || !frame || !control)
                return TraversalReadError::invalid;
            document.visits.push_back({*seq, *frame, *control});
        }
        for (const auto &node : *resumes) {
            const auto &entry = *node.as_table();
            const auto seq = unsigned_value(entry["seq"]);
            const auto from = unsigned_value(entry["from_frame"]);
            const auto to = unsigned_value(entry["to"]);
            const auto mode = entry["mode"].value<std::string>();
            if (!seq || !from || !to || !mode)
                return TraversalReadError::invalid;
            document.resume_after.push_back({*seq, *from, *to, *mode});
        }
        for (const auto &node : *interruptions) {
            const auto &entry = *node.as_table();
            const auto seq = unsigned_value(entry["seq"]);
            const auto frame = unsigned_value(entry["frame"]);
            if (!seq || !frame)
                return TraversalReadError::invalid;
            document.interruptions.push_back({*seq, *frame});
        }
        return document;
    } catch (const toml::parse_error &) {
        return TraversalReadError::invalid;
    }
}

TraversalReadResult read_traversal(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return error ? TraversalReadError::unreadable : TraversalReadError::missing;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > max_traversal_bytes)
        return TraversalReadError::unreadable;
    std::ifstream input{path, std::ios::binary};
    if (!input)
        return TraversalReadError::unreadable;
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return parse_traversal(text);
}

DurablePublishResult write_traversal(const std::filesystem::path &path,
                                     const TraversalDocument &document,
                                     DurablePublicationLimits limits) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return DurablePublishError::invalid_target;
    const auto text = format_traversal(document);
    return publish_durable_file(path, std::as_bytes(std::span{text.data(), text.size()}), limits);
}

} // namespace ayther::audio_qa
