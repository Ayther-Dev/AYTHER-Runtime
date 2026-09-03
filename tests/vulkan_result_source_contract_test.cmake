if(NOT DEFINED RUNTIME_SOURCE_DIR OR RUNTIME_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "RUNTIME_SOURCE_DIR is required")
endif()

set(result_calls
    vmaCreateAllocator
    vkAcquireNextImageKHR
    vkAllocateCommandBuffers
    vkAllocateDescriptorSets
    vkBeginCommandBuffer
    vkCreateCommandPool
    vkCreateDescriptorPool
    vkCreateDescriptorSetLayout
    vkCreateFence
    vkCreateFramebuffer
    vkCreateGraphicsPipelines
    vkCreateImageView
    vkCreatePipelineLayout
    vkCreateRenderPass
    vkCreateSampler
    vkCreateSemaphore
    vkCreateShaderModule
    vkDeviceWaitIdle
    vkEndCommandBuffer
    vkQueuePresentKHR
    vkQueueSubmit
    vkResetCommandPool
    vkResetFences
    vkWaitForFences
)

file(GLOB_RECURSE runtime_sources LIST_DIRECTORIES false
     "${RUNTIME_SOURCE_DIR}/*.cpp" "${RUNTIME_SOURCE_DIR}/*.h")
set(offenders "")
foreach(source_file IN LISTS runtime_sources)
    get_filename_component(source_name "${source_file}" NAME)
    if(source_name STREQUAL "vulkan_calls.cpp")
        continue()
    endif()

    file(READ "${source_file}" source_text)
    foreach(call_name IN LISTS result_calls)
        if(source_text MATCHES "${call_name}[ \t\r\n]*\\(")
            list(APPEND offenders "${source_file}: ${call_name}")
        endif()
    endforeach()
endforeach()

if(offenders)
    list(REMOVE_DUPLICATES offenders)
    list(JOIN offenders "\n  " offender_list)
    message(FATAL_ERROR
        "VkResult calls must pass through vulkan_calls.cpp:\n  ${offender_list}")
endif()
