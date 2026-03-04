include(FetchContent)

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

# VMA (Vulkan Memory Allocator)
add_library(vma INTERFACE)

target_include_directories(vma
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/vma
)

if (WIN32)

    FetchContent_Declare(
        d3d12ma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
        GIT_TAG v3.1.0
    )

    FetchContent_MakeAvailable(d3d12ma)

    add_library(d3d12ma INTERFACE)

    target_include_directories(d3d12ma
        SYSTEM INTERFACE
            ${d3d12ma_SOURCE_DIR}/include
    )

endif()

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