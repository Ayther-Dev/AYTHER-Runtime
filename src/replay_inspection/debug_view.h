#pragma once

#include "frame_record.h"
#include "replay_messages.h"
#include "take_clock.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, plan §5.9 and D15 (RF-7.1, RF-7.2, RF-7.3, RF-7.7, RNF-3, RNF-4): the lines the debug
// overlay draws for one FrameRecord. The labels carry the units; the values are never cut and
// a measure that is not known is not drawn.
struct DebugLine {
    std::string label;
    std::string value;
};

[[nodiscard]] std::vector<DebugLine> debug_lines(const FrameRecord &record, const TakeClock &clock,
                                                 ReplayLanguage language);

// The overlay scrolls with the wheel and with Page Up, Page Down, Home and End. Space, the
// arrows and I are never scroll keys: they always belong to the inspection.
enum class ScrollKey { wheel_up, wheel_down, page_up, page_down, home, end };

[[nodiscard]] std::optional<ScrollKey> scroll_key_of_name(std::string_view name) noexcept;

class DebugScroll final {
  public:
    void apply(ScrollKey key, std::size_t visible_lines, std::size_t total_lines) noexcept;
    [[nodiscard]] std::size_t first() const noexcept { return first_; }
    void reset() noexcept { first_ = 0; }

  private:
    std::size_t first_{};
};

} // namespace ayther::replay_inspection
