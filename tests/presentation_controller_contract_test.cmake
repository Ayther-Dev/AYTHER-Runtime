cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS CONTROLLER_HEADER APPLICATION_SOURCE)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(READ "${CONTROLLER_HEADER}" controller)
file(READ "${APPLICATION_SOURCE}" application)

foreach(member IN ITEMS "VkContext context_" "VkSwapchain swapchain_"
                        "VkPostProcess postprocess_"
                        "ayther::PlayerOverlay overlay_")
    string(FIND "${controller}" "${member}" member_at)
    if(member_at EQUAL -1)
        message(FATAL_ERROR "PresentationController does not own '${member}'")
    endif()
endforeach()

set(previous -1)
foreach(operation IN ITEMS "overlay_.shutdown(context_)"
                           "postprocess_.shutdown(context_)"
                           "swapchain_.shutdown()" "context_.shutdown()")
    string(FIND "${controller}" "${operation}" operation_at)
    if(operation_at EQUAL -1 OR operation_at LESS previous)
        message(FATAL_ERROR
            "Presentation teardown is missing or not in reverse dependency order: ${operation}")
    endif()
    set(previous ${operation_at})
endforeach()

foreach(forbidden IN ITEMS "overlay.shutdown(" "postprocess.shutdown("
                           "presentation.shutdown(")
    string(FIND "${application}" "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR
            "RuntimeApplication still requires manual presentation teardown: ${forbidden}")
    endif()
endforeach()

foreach(accessor IN ITEMS "presentation.context()" "presentation.swapchain()"
                          "presentation.postprocess()" "presentation.overlay()")
    string(FIND "${application}" "${accessor}" accessor_at)
    if(accessor_at EQUAL -1)
        message(FATAL_ERROR "RuntimeApplication does not compose '${accessor}'")
    endif()
endforeach()
