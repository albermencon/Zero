include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# Moodycamel queues (header-only)
add_library(moodycamel INTERFACE)

target_include_directories(moodycamel
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/moodycamel
)

# VMA (Vulkan Memory Allocator)
add_library(vma INTERFACE)

target_include_directories(vma
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/vma/include
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
add_subdirectory(GLFW)
add_subdirectory(glad)
add_subdirectory(highway)

#slang
include(ExternalProject)

set(SLANG_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/slang")
set(SLANG_BINARY_DIR  "${CMAKE_BINARY_DIR}/slang-build")

# Detect config (multi-config vs single-config)

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(IS_MULTI_CONFIG)
    set(SLANG_CONFIG "$<CONFIG>")
else()
    set(SLANG_CONFIG "${CMAKE_BUILD_TYPE}")
    if(NOT SLANG_CONFIG)
        set(SLANG_CONFIG "Release")
    endif()
endif()

set(SLANG_LIB_DIR "${SLANG_BINARY_DIR}/Release/lib")

# Platform extensions
if(WIN32)
    set(SLANG_SHARED_EXT ".dll")
    set(SLANG_IMPLIB_EXT ".lib")
    set(SLANG_PREFIX "")
elseif(APPLE)
    set(SLANG_SHARED_EXT ".dylib")
    set(SLANG_PREFIX "lib")
else()
    set(SLANG_SHARED_EXT ".so")
    set(SLANG_PREFIX "lib")
endif()

# Library names

set(SLANG_COMPILER_NAME "slang-compiler")
set(SLANG_RT_NAME       "slang-rt")

# Full paths

set(SLANG_COMPILER_PATH
    "${SLANG_LIB_DIR}/${SLANG_PREFIX}${SLANG_COMPILER_NAME}${SLANG_SHARED_EXT}"
)

set(SLANG_RT_PATH
    "${SLANG_LIB_DIR}/${SLANG_PREFIX}${SLANG_RT_NAME}${SLANG_SHARED_EXT}"
)

# build byproducts
if(WIN32)
    set(_SLANG_BYPRODUCTS
        "${SLANG_BINARY_DIR}/Release/lib/${SLANG_COMPILER_NAME}.dll"
        "${SLANG_BINARY_DIR}/Release/lib/${SLANG_RT_NAME}.dll"
    )
elseif(APPLE)
    set(_SLANG_BYPRODUCTS
        "${SLANG_BINARY_DIR}/Release/lib/lib${SLANG_COMPILER_NAME}.dylib"
        "${SLANG_BINARY_DIR}/Release/lib/lib${SLANG_RT_NAME}.dylib"
    )
else()
    set(_SLANG_BYPRODUCTS
        "${SLANG_BINARY_DIR}/Release/lib/lib${SLANG_COMPILER_NAME}.so"
        "${SLANG_BINARY_DIR}/Release/lib/lib${SLANG_RT_NAME}.so"
    )
endif()

# ExternalProject

ExternalProject_Add(slang_build
    SOURCE_DIR "${SLANG_SOURCE_DIR}"
    BINARY_DIR "${SLANG_BINARY_DIR}"

    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}

        -DSLANG_ENABLE_SLANG_RHI=OFF
        -DSLANG_ENABLE_GFX=OFF
        -DSLANG_ENABLE_SLANGD=OFF
        -DSLANG_ENABLE_SLANGI=OFF
        -DSLANG_ENABLE_TESTS=OFF
        -DSLANG_ENABLE_EXAMPLES=OFF
        -DSLANG_SLANG_LLVM_FLAVOR=DISABLE

    BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
    INSTALL_COMMAND ""

    BUILD_BYPRODUCTS ${_SLANG_BYPRODUCTS}
)

# IMPORTED TARGETS

add_library(slang::compiler SHARED IMPORTED GLOBAL)
add_dependencies(slang::compiler slang_build)

add_library(slang::rt SHARED IMPORTED GLOBAL)
add_dependencies(slang::rt slang_build)

if(WIN32)
    set_target_properties(slang::compiler PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB_DIR}/${SLANG_COMPILER_NAME}${SLANG_SHARED_EXT}"
        IMPORTED_IMPLIB   "${SLANG_LIB_DIR}/${SLANG_COMPILER_NAME}${SLANG_IMPLIB_EXT}"
    )

    set_target_properties(slang::rt PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB_DIR}/${SLANG_RT_NAME}${SLANG_SHARED_EXT}"
        IMPORTED_IMPLIB   "${SLANG_LIB_DIR}/${SLANG_RT_NAME}${SLANG_IMPLIB_EXT}"
    )
else()
    set_target_properties(slang::compiler PROPERTIES
        IMPORTED_LOCATION "${SLANG_COMPILER_PATH}"
    )

    set_target_properties(slang::rt PROPERTIES
        IMPORTED_LOCATION "${SLANG_RT_PATH}"
    )
endif()

target_include_directories(slang::compiler
    INTERFACE "${SLANG_SOURCE_DIR}/include"
)

# end of slang

# joltphysics
set(JPH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(JPH_ENABLE_ASSERTS OFF CACHE BOOL "" FORCE) # optional
set(JPH_ENABLE_DEBUG_RENDERER OFF CACHE BOOL "" FORCE)
set(JPH_OBJECT_STREAM OFF CACHE BOOL "" FORCE) # optional
set(JPH_USE_SSE4_1 ON CACHE BOOL "" FORCE)
set(JPH_USE_AVX2 OFF CACHE BOOL "" FORCE)

add_subdirectory(joltphysics/Build)

#end joltphysics

# Add the Highway include directory explicitly for safety
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/highway)

find_package(Vulkan REQUIRED)

set(IMGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_vulkan.cpp

)

target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

target_link_libraries(imgui
    PUBLIC
        Vulkan::Vulkan
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