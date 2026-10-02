#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ayther::runtime {

enum class MusicWarningCode {
  analysis_obsolete,
  game_pause_unobservable,
  analysis_limit,
  incompatible_pack,
  restore_failed_rolled_back,
};

[[nodiscard]] constexpr std::string_view
music_warning_code(const MusicWarningCode code) noexcept {
  switch (code) {
  case MusicWarningCode::analysis_obsolete: return "analysis_obsolete";
  case MusicWarningCode::game_pause_unobservable: return "game_pause_unobservable";
  case MusicWarningCode::analysis_limit: return "analysis_limit";
  case MusicWarningCode::incompatible_pack: return "incompatible_pack";
  case MusicWarningCode::restore_failed_rolled_back: return "restore_failed_rolled_back";
  }
  return "unknown_music_warning";
}

[[nodiscard]] inline std::string music_warning_message(
    const MusicWarningCode code, const std::string_view language,
    const std::string_view product) {
  const bool english = language == "en";
  std::string text;
  switch (code) {
  case MusicWarningCode::analysis_obsolete:
    text = english ? "The analysis revision is obsolete and was ignored"
                   : "La revisión de análisis quedó obsoleta y fue ignorada";
    break;
  case MusicWarningCode::game_pause_unobservable:
    text = english ? "The game pause cannot be observed; music keeps advancing"
                   : "La pausa del juego no puede observarse; la música continúa";
    break;
  case MusicWarningCode::analysis_limit:
    text = english ? "The music analysis reached a configured limit"
                   : "El análisis musical alcanzó un límite configurado";
    break;
  case MusicWarningCode::incompatible_pack:
    text = english ? "The pack is incompatible; original audio remains active"
                   : "El pack es incompatible; continúa el audio original";
    break;
  case MusicWarningCode::restore_failed_rolled_back:
    text = english ? "Music restore failed and the previous state was recovered"
                   : "Falló la restauración musical y se recuperó el estado anterior";
    break;
  }
  text += " · ";
  text += product;
  text += " · ";
  text += music_warning_code(code);
  return text;
}

struct MusicWarning {
  MusicWarningCode code{MusicWarningCode::analysis_limit};
  std::string logical_identity;
  std::string detail;
  std::uint64_t monotonic_ns{};
};

struct MusicWarningAggregate {
  MusicWarning warning;
  std::uint64_t count{};
  std::uint64_t first_ns{};
  std::uint64_t last_ns{};
  std::string detail;
};

class MusicWarningAggregator final {
public:
  explicit MusicWarningAggregator(const std::uint64_t minimum_period_ns)
      : minimum_period_ns_(minimum_period_ns) {}

  [[nodiscard]] std::optional<MusicWarningAggregate> push(const MusicWarning &warning) {
    full_trace_.push_back(warning);
    const std::string condition = key(warning);
    auto [position, inserted] = states_.try_emplace(condition);
    State &state = position->second;
    if (inserted || warning.monotonic_ns < state.last_emit_ns) {
      state = State{warning, warning.monotonic_ns, 0, 0, 0};
      return MusicWarningAggregate{warning, 1, warning.monotonic_ns,
                                   warning.monotonic_ns, warning.detail};
    }
    if (state.pending == 0)
      state.pending_first_ns = warning.monotonic_ns;
    ++state.pending;
    state.pending_last_ns = warning.monotonic_ns;
    state.last = warning;
    if (warning.monotonic_ns - state.last_emit_ns < minimum_period_ns_)
      return std::nullopt;
    const auto aggregate = pending_aggregate(state);
    state.pending = 0;
    state.last_emit_ns = warning.monotonic_ns;
    return aggregate;
  }

  [[nodiscard]] std::vector<MusicWarningAggregate> close(const std::uint64_t) {
    std::vector<MusicWarningAggregate> result;
    for (auto &[unused, state] : states_) {
      (void)unused;
      if (state.pending != 0) {
        result.push_back(pending_aggregate(state));
        state.pending = 0;
      }
    }
    return result;
  }

  [[nodiscard]] const std::vector<MusicWarning> &full_trace() const noexcept {
    return full_trace_;
  }

private:
  struct State {
    MusicWarning last;
    std::uint64_t last_emit_ns{};
    std::uint64_t pending{};
    std::uint64_t pending_first_ns{};
    std::uint64_t pending_last_ns{};
  };

  [[nodiscard]] static std::string key(const MusicWarning &warning) {
    std::string result{music_warning_code(warning.code)};
    result.push_back('\0');
    result += warning.logical_identity;
    result.push_back('\0');
    result += warning.detail;
    return result;
  }

  [[nodiscard]] static MusicWarningAggregate pending_aggregate(const State &state) {
    return {state.last, state.pending, state.pending_first_ns,
            state.pending_last_ns, state.last.detail};
  }

  std::uint64_t minimum_period_ns_{};
  std::unordered_map<std::string, State> states_;
  std::vector<MusicWarning> full_trace_;
};

} // namespace ayther::runtime
