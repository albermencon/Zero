# Prevent vendor libraries from installing themselves
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "" FORCE)

# Highway specific settings - disable everything we don't need
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_PRINT_TARGETS OFF CACHE BOOL "" FORCE)
set(HWY_SYSTEM_GTEST OFF CACHE BOOL "" FORCE)  # Google Tests off
set(HWY_SYSTEM_JPEG OFF CACHE BOOL "" FORCE)   # No image codecs needed
set(HWY_SYSTBM OFF CACHE BOOL "" FORCE)        # No TinyEXR

# Add submodules
add_subdirectory(spdlog)
add_subdirectory(glfw)
add_subdirectory(glad)
add_subdirectory(highway)

# Add the Highway include directory explicitly for safety
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/highway)

set(IMGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)

target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

if (ENGINE_ENABLE_PROFILING)

    # Prevent Tracy from installing itself
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)
    set(TRACY_CALLSTACK 10 CACHE STRING "" FORCE)
    set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
    set(TRACY_STATIC ON CACHE BOOL "" FORCE)

    add_subdirectory(tracy)

endif()