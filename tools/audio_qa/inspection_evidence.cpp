#include "inspection_evidence.h"

#include "long_path.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <system_error>

namespace ayther::audio_qa {
namespace {

constexpr std::int64_t traversal_schema_version = 1;
// 1.2 (spec 002, DI-14) adds `audio_segments` and (DI-15) `fact_exclusions`; a 1.1 document,
// or an earlier 1.2 one, reads without them.
constexpr std::int64_t traversal_schema_minor = 2;
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

// 64-bit sample positions are decimal strings (contracts.md C2).
std::optional<std::uint64_t> decimal_value(const toml::node_view<const toml::node> node) {
    const auto text = node.value<std::string>();
    if (!text || text->empty() || text->front() == '+' || text->front() == '-')
        return std::nullopt;
    std::uint64_t value{};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size())
        return std::nullopt;
    return value;
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
    if (visit_limit_exceeded_ || document_.visits.size() >= max_traversal_visits) {
        visit_limit_exceeded_ = true;
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

bool TraversalRecorder::visit_limit_exceeded() const noexcept { return visit_limit_exceeded_; }

bool exceeds_visit_limit(std::span<const InspectionEvent> events) noexcept {
    const auto visits = std::count_if(events.begin(), events.end(), [](const auto &event) {
        return event.control != "interrupted";
    });
    return static_cast<std::uint64_t>(visits) > max_traversal_visits;
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

AudioSegmentsResult audio_segments_of_take(const TraversalDocument &traversal,
                                           std::span<const InspectionEvent> events,
                                           std::span<const PcmSegmentInterval> pcm) {
    // The frames of each audio segment: the stretches of the traversal played in it.
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> frames;
    const auto played = [&frames](std::uint64_t segment, std::uint64_t from, std::uint64_t to) {
        const auto [entry, inserted] = frames.try_emplace(segment, from, to);
        if (!inserted) {
            entry->second.first = std::min(entry->second.first, from);
            entry->second.second = std::max(entry->second.second, to);
        }
    };
    for (const auto &segment : traversal.segments)
        played(0U, segment.from, segment.to);
    // The Runtime opens segment n at the n-th resume; a stretch continued after another
    // control (the overlay toggled while playing) stays in the segment of the last resume.
    std::vector<std::uint64_t> resumes;
    for (const auto &event : events)
        if (event.control == "resume")
            resumes.push_back(event.seq);
    std::sort(resumes.begin(), resumes.end());
    for (const auto &resume : traversal.resume_after) {
        const auto segment = static_cast<std::uint64_t>(
            std::upper_bound(resumes.begin(), resumes.end(), resume.seq) - resumes.begin());
        played(segment, resume.from_frame, resume.to);
    }

    std::vector<TraversalAudioSegment> segments;
    for (const auto &kept : pcm) {
        const auto found = frames.find(kept.segment);
        if (found == frames.end())
            return AudioSegmentsError::pcm_without_frames;
        segments.push_back({kept.segment, found->second.first, found->second.second,
                            kept.samples.timeline_id, kept.samples.sample_rate, kept.samples.begin,
                            kept.samples.end, kept.blocks});
    }
    for (const auto &[segment, interval] : frames)
        if (std::none_of(pcm.begin(), pcm.end(), [segment](const PcmSegmentInterval &kept) {
                return kept.segment == segment;
            }))
            return AudioSegmentsError::frames_without_pcm;
    return segments;
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
    toml::table table{{"schema_version", traversal_schema_version},
                      {"schema_minor", traversal_schema_minor},
                      {"kind", std::string{traversal_kind_code(document.kind)}},
                      {"linear_completed", document.linear_completed},
                      {"frames_total", static_cast<std::int64_t>(document.frames_total)},
                      {"segments", std::move(segments)},
                      {"visits", std::move(visits)},
                      {"resume_after", std::move(resumes)},
                      {"interruptions", std::move(interruptions)}};
    if (document.audio_segments) {
        toml::array audio;
        for (const auto &segment : *document.audio_segments)
            audio.push_back(
                toml::table{{"segment", static_cast<std::int64_t>(segment.segment)},
                            {"frame_from", static_cast<std::int64_t>(segment.frame_from)},
                            {"frame_to", static_cast<std::int64_t>(segment.frame_to)},
                            {"timeline", segment.timeline},
                            {"sample_rate", static_cast<std::int64_t>(segment.sample_rate)},
                            {"sample_begin", std::to_string(segment.sample_begin)},
                            {"sample_end", std::to_string(segment.sample_end)},
                            {"pcm_blocks", static_cast<std::int64_t>(segment.pcm_blocks)}});
        table.insert("audio_segments", std::move(audio));
    }
    if (document.fact_exclusions) {
        toml::array exclusions;
        for (const auto &exclusion : *document.fact_exclusions)
            exclusions.push_back(
                toml::table{{"recovery", static_cast<std::int64_t>(exclusion.recovery)},
                            {"producer", exclusion.producer},
                            {"sequence_from", std::to_string(exclusion.sequence_from)},
                            {"sequence_to", std::to_string(exclusion.sequence_to)},
                            {"cause", exclusion.cause}});
        table.insert("fact_exclusions", std::move(exclusions));
    }
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
        // DI-14: absent in 1.1; when present every entry is complete.
        if (table.contains("audio_segments")) {
            const auto *audio = tables(table, "audio_segments");
            if (audio == nullptr)
                return TraversalReadError::invalid;
            std::vector<TraversalAudioSegment> audio_segments;
            for (const auto &node : *audio) {
                const auto &entry = *node.as_table();
                const auto segment = unsigned_value(entry["segment"]);
                const auto from = unsigned_value(entry["frame_from"]);
                const auto to = unsigned_value(entry["frame_to"]);
                const auto timeline = entry["timeline"].value<std::string>();
                const auto rate = unsigned_value(entry["sample_rate"]);
                const auto begin = decimal_value(entry["sample_begin"]);
                const auto end = decimal_value(entry["sample_end"]);
                const auto blocks = unsigned_value(entry["pcm_blocks"]);
                if (!segment || !from || !to || !timeline || !rate ||
                    *rate > std::numeric_limits<std::uint32_t>::max() || !begin || !end ||
                    *begin > *end || !blocks)
                    return TraversalReadError::invalid;
                audio_segments.push_back({*segment, *from, *to, *timeline,
                                          static_cast<std::uint32_t>(*rate), *begin, *end,
                                          *blocks});
            }
            document.audio_segments = std::move(audio_segments);
        }
        // DI-15: absent in 1.1 and in an earlier 1.2; when present every entry is complete.
        if (table.contains("fact_exclusions")) {
            const auto *declared = tables(table, "fact_exclusions");
            if (declared == nullptr)
                return TraversalReadError::invalid;
            std::vector<FactExclusion> exclusions;
            for (const auto &node : *declared) {
                const auto &entry = *node.as_table();
                const auto recovery = unsigned_value(entry["recovery"]);
                const auto producer = entry["producer"].value<std::string>();
                const auto from = decimal_value(entry["sequence_from"]);
                const auto to = decimal_value(entry["sequence_to"]);
                const auto cause = entry["cause"].value<std::string>();
                if (!recovery || *recovery == 0U || !producer ||
                    !producer->starts_with("engine-") || !from || *from == 0U || !to ||
                    *from > *to || !cause || *cause != silent_recovery_cause)
                    return TraversalReadError::invalid;
                exclusions.push_back({*recovery, *producer, *from, *to, *cause});
            }
            document.fact_exclusions = std::move(exclusions);
        }
        return document;
    } catch (const toml::parse_error &) {
        return TraversalReadError::invalid;
    }
}

TraversalReadResult read_traversal(const std::filesystem::path &document_path) {
    // D-12: read through the extended form of the path, which has no MAX_PATH limit.
    const auto path = long_path(document_path);
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
    std::filesystem::create_directories(long_path(path.parent_path()), error);
    if (error)
        return DurablePublishError::invalid_target;
    const auto text = format_traversal(document);
    return publish_durable_file(path, std::as_bytes(std::span{text.data(), text.size()}), limits);
}

} // namespace ayther::audio_qa
