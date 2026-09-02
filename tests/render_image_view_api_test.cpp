#include <ayther/engine/vulkan_interop.hpp>

#include <cstdint>
#include <cstdio>
#include <type_traits>

int main() {
    using ayther::engine::RenderImageView;

    static_assert(std::is_standard_layout_v<RenderImageView>);
    static_assert(std::is_trivially_copyable_v<RenderImageView>);
    static_assert(std::is_same_v<decltype(RenderImageView::image), VkImage>);
    static_assert(std::is_same_v<decltype(RenderImageView::layout),
                                 VkImageLayout>);
    static_assert(std::is_same_v<decltype(RenderImageView::queue_family_index),
                                 std::uint32_t>);

    const RenderImageView empty{};
    const bool safe_default = !empty.is_valid() &&
                              empty.image == VK_NULL_HANDLE &&
                              empty.image_view == VK_NULL_HANDLE &&
                              empty.layout == VK_IMAGE_LAYOUT_UNDEFINED &&
                              empty.queue_family_index ==
                                  VK_QUEUE_FAMILY_IGNORED;
    std::printf("  [%s] installed RenderImageView has an inert default\n",
                safe_default ? " OK " : "FAIL");
    return safe_default ? 0 : 1;
}
