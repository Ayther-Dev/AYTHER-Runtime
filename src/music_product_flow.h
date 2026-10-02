#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace ayther::runtime {

struct ProductSampleRange {
  std::uint64_t begin{};
  std::uint64_t end{};
  [[nodiscard]] constexpr bool valid() const noexcept { return begin < end; }
  friend constexpr bool operator==(ProductSampleRange, ProductSampleRange) = default;
};

enum class ProductFlowStage { empty, range, authored, previewed, baked, runtime };

struct ProductFlowState {
  ProductFlowStage stage{ProductFlowStage::empty};
  ProductSampleRange range{};
  std::string identity;
  std::string pack_revision;
  std::string save;
  std::string take{"take-1"};
  std::uint64_t cursor{};
  bool host_paused{};
  friend bool operator==(const ProductFlowState &, const ProductFlowState &) = default;
};

class MusicProductFlow final {
public:
  [[nodiscard]] bool select_range(const ProductSampleRange range) {
    if (!range.valid())
      return false;
    working_ = accepted_;
    working_.range = range;
    working_.stage = ProductFlowStage::range;
    accepted_ = working_;
    return true;
  }

  [[nodiscard]] bool author(std::string identity) {
    if (accepted_.stage != ProductFlowStage::range || identity.empty())
      return false;
    accepted_.identity = std::move(identity);
    accepted_.stage = ProductFlowStage::authored;
    return true;
  }
  [[nodiscard]] bool preview() {
    if (accepted_.stage != ProductFlowStage::authored)
      return false;
    accepted_.stage = ProductFlowStage::previewed;
    return true;
  }
  [[nodiscard]] bool bake(std::string revision) {
    if (accepted_.stage != ProductFlowStage::previewed || revision.empty())
      return false;
    accepted_.pack_revision = std::move(revision);
    accepted_.stage = ProductFlowStage::baked;
    return true;
  }
  [[nodiscard]] bool start_runtime() {
    if (accepted_.stage != ProductFlowStage::baked)
      return false;
    accepted_.stage = ProductFlowStage::runtime;
    replay_id_ = "replay:" + accepted_.pack_revision + ":" + accepted_.take;
    return true;
  }
  [[nodiscard]] bool pause_host(const bool paused) {
    if (accepted_.stage != ProductFlowStage::runtime)
      return false;
    accepted_.host_paused = paused;
    return true;
  }
  [[nodiscard]] bool save(std::string save_id) {
    if (accepted_.stage != ProductFlowStage::runtime || save_id.empty())
      return false;
    accepted_.save = std::move(save_id);
    return true;
  }
  [[nodiscard]] bool cancel_analysis(const std::string &) noexcept {
    working_ = accepted_;
    return false;
  }
  [[nodiscard]] bool navigate(std::string take) {
    if (accepted_.stage != ProductFlowStage::runtime || take.empty())
      return false;
    accepted_.take = std::move(take);
    accepted_.cursor = 0;
    accepted_.save.clear();
    navigation_id_ = "navigation:" + accepted_.take;
    return true;
  }
  [[nodiscard]] const ProductFlowState &accepted_state() const noexcept { return accepted_; }
  [[nodiscard]] const std::string &replay_id() const noexcept { return replay_id_; }
  [[nodiscard]] const std::string &navigation_id() const noexcept { return navigation_id_; }

private:
  ProductFlowState accepted_;
  ProductFlowState working_;
  std::string replay_id_;
  std::string navigation_id_;
};

} // namespace ayther::runtime
