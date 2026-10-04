#include "debug_view.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace ayther::replay_inspection {
namespace {

std::string decimal(double value) {
    std::array<char, 64> text{};
    const auto written = std::snprintf(text.data(), text.size(), "%.2f", value);
    return written > 0 ? std::string{text.data(), static_cast<std::size_t>(written)} : "";
}

std::string hexadecimal(std::uint64_t value) {
    std::array<char, 32> text{};
    const auto written =
        std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
    return written > 0 ? std::string{text.data(), static_cast<std::size_t>(written)} : "";
}

std::string_view status_code(render::OccurrenceStatus status) noexcept {
    switch (status) {
    case render::OccurrenceStatus::replaced:
        return "replaced";
    case render::OccurrenceStatus::original_unassigned:
        return "original_unassigned";
    case render::OccurrenceStatus::assigned_not_applied:
        return "assigned_not_applied";
    case render::OccurrenceStatus::hidden_by_author:
        return "hidden_by_author";
    }
    return "unknown";
}

std::string_view draw_code(render::DrawOutcome draw) noexcept {
    switch (draw) {
    case render::DrawOutcome::in_pass:
        return "in_pass";
    case render::DrawOutcome::partitioned:
        return "partitioned";
    case render::DrawOutcome::lane:
        return "lane";
    case render::DrawOutcome::discarded:
        return "discarded";
    }
    return "unknown";
}

std::string text(ReplayLanguage language, ReplayMessage message) {
    return std::string{replay_message(language, message)};
}

} // namespace

std::vector<DebugLine> debug_lines(const FrameRecord &record, const TakeClock &clock,
                                   ReplayLanguage language) {
    using enum ReplayMessage;
    const auto times = clock.at(record.frame);
    std::vector<DebugLine> lines{
        {text(language, label_rom), record.general.rom},
        {text(language, label_take), record.general.take},
        {text(language, label_pack), record.general.pack.value_or(text(language, no_pack))},
        {text(language, label_phase), record.general.phase},
        {text(language, label_frame), std::to_string(record.frame)},
        {text(language, label_total_frames), std::to_string(record.general.total_frames)},
        {text(language, label_elapsed), decimal(times.elapsed_ms)},
        {text(language, label_remaining), decimal(times.remaining_ms)},
    };
    // RF-7.7: only the measures that exist for this visit.
    if (record.measurements.processing_ms)
        lines.push_back(
            {text(language, label_processing), decimal(*record.measurements.processing_ms)});
    if (record.measurements.fps_instant)
        lines.push_back({text(language, label_fps), decimal(*record.measurements.fps_instant)});
    lines.push_back({text(language, label_occurrences), std::to_string(record.rows.size())});
    for (const auto &row : record.rows) {
        std::string value = "slot " + std::to_string(row.slot) + " chain " +
                            std::to_string(row.chain) + " id " + hexadecimal(row.identity) + " " +
                            std::string{status_code(row.status)};
        if (row.pose)
            value += " pose " + *row.pose;
        if (row.asset)
            value += " asset " + *row.asset;
        if (row.draw)
            value += " draw " + std::string{draw_code(*row.draw)};
        if (row.reason)
            value += " reason " + *row.reason;
        lines.push_back({"#" + std::to_string(row.index), std::move(value)});
    }
    if (record.overflow)
        lines.push_back({text(language, label_overflow),
                         std::to_string(record.overflow->rows_total - record.overflow->limit) +
                             " (" + std::to_string(record.overflow->rows_total) + " > " +
                             std::to_string(record.overflow->limit) + ")"});
    return lines;
}

std::optional<ScrollKey> scroll_key_of_name(std::string_view name) noexcept {
    constexpr std::array<std::pair<std::string_view, ScrollKey>, 6> keys{{
        {"wheel_up", ScrollKey::wheel_up},
        {"wheel_down", ScrollKey::wheel_down},
        {"page_up", ScrollKey::page_up},
        {"page_down", ScrollKey::page_down},
        {"home", ScrollKey::home},
        {"end", ScrollKey::end},
    }};
    for (const auto &[text_name, key] : keys)
        if (name == text_name)
            return key;
    return std::nullopt;
}

void DebugScroll::apply(ScrollKey key, std::size_t visible_lines,
                        std::size_t total_lines) noexcept {
    constexpr std::size_t wheel_lines = 3;
    const std::size_t last = total_lines > visible_lines ? total_lines - visible_lines : 0U;
    const std::size_t page = std::max<std::size_t>(visible_lines, 1U);
    switch (key) {
    case ScrollKey::wheel_up:
        first_ = first_ > wheel_lines ? first_ - wheel_lines : 0U;
        break;
    case ScrollKey::wheel_down:
        first_ += wheel_lines;
        break;
    case ScrollKey::page_up:
        first_ = first_ > page ? first_ - page : 0U;
        break;
    case ScrollKey::page_down:
        first_ += page;
        break;
    case ScrollKey::home:
        first_ = 0U;
        break;
    case ScrollKey::end:
        first_ = last;
        break;
    }
    first_ = std::min(first_, last);
}

} // namespace ayther::replay_inspection
