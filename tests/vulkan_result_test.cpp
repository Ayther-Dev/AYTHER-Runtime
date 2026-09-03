#include "vulkan_backend/vk_result.h"

#include <cstdio>
#include <string>
#include <type_traits>

namespace {

int passed = 0;
int failed = 0;

void check(const bool condition, const char* description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::fprintf(stderr, "  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

}  // namespace

int main() {
    using ayther::runtime::vulkan::format_vk_failure;
    using ayther::runtime::vulkan::require_vk_success;
    using ayther::runtime::vulkan::vk_failure;
    using ayther::runtime::vulkan::vk_result_symbol;
    using ayther::runtime::vulkan::VkFailure;

    std::fprintf(stderr, "== vulkan_result_test (MAD-008) ==\n");

    check(!vk_failure("vkQueueSubmit", VK_SUCCESS).has_value(),
          "VK_SUCCESS has no failure value");

    const auto failure = vk_failure("vkQueueSubmit", VK_ERROR_DEVICE_LOST);
    check(failure.has_value(), "a failing VkResult produces a typed failure");
    if (failure.has_value()) {
        check(failure->operation == "vkQueueSubmit",
              "the operation name survives in the typed failure");
        check(failure->code == VK_ERROR_DEVICE_LOST,
              "the numeric VkResult survives in the typed failure");
    }

    check(vk_result_symbol(VK_TIMEOUT) == "VK_TIMEOUT",
          "a positive status has a symbolic name");
    check(vk_result_symbol(VK_SUBOPTIMAL_KHR) == "VK_SUBOPTIMAL_KHR",
          "a KHR status has a symbolic name");
    check(vk_result_symbol(VK_ERROR_OUT_OF_DATE_KHR) == "VK_ERROR_OUT_OF_DATE_KHR",
          "a KHR error has a symbolic name");
    check(vk_result_symbol(static_cast<VkResult>(-987654)) == "VK_RESULT_UNKNOWN",
          "unknown driver values retain an explicit symbolic fallback");

    const std::string formatted =
        format_vk_failure(VkFailure{"vkQueueSubmit", VK_ERROR_DEVICE_LOST});
    check(formatted.find("vkQueueSubmit") != std::string::npos,
          "formatted failure includes the operation");
    check(formatted.find("VK_ERROR_DEVICE_LOST") != std::string::npos,
          "formatted failure includes the symbolic VkResult");
    check(formatted.find("-4") != std::string::npos,
          "formatted failure includes the integer VkResult");

    check(require_vk_success("vkQueueSubmit", VK_SUCCESS),
          "the central checker accepts VK_SUCCESS");
    check(!require_vk_success("vkQueueSubmit", VK_ERROR_DEVICE_LOST),
          "the central checker rejects and reports a Vulkan error");

    std::fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
