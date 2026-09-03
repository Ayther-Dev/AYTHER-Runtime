#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <string>
#include <string_view>

namespace ayther::runtime::vulkan {

struct [[nodiscard]] VkFailure final {
    std::string_view operation;
    VkResult code{VK_SUCCESS};
};

[[nodiscard]] std::optional<VkFailure> vk_failure(std::string_view operation,
                                                  VkResult code) noexcept;

[[nodiscard]] std::string_view vk_result_symbol(VkResult code) noexcept;

[[nodiscard]] std::string format_vk_failure(const VkFailure& failure);

void log_vk_failure(const VkFailure& failure) noexcept;

/// Returns true for VK_SUCCESS. Every other code is logged with its operation,
/// symbolic name, and integer value before false is returned.
[[nodiscard]] bool require_vk_success(std::string_view operation,
                                      VkResult code) noexcept;

}  // namespace ayther::runtime::vulkan
