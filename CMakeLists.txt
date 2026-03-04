cmake_minimum_required(VERSION 3.20)
project(VoxelEngine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED "Build as shared library" ON)
option(ENGINE_ENABLE_PROFILING "Enable Tracy profiling" ON) # profiling with tracy

file(GLOB_RECURSE ENGINE_SOURCES
    "src/*.cpp"
)

# Add vendor dependencies
add_subdirectory(third_party)

if(BUILD_SHARED)
    add_library(VoxelEngine SHARED ${ENGINE_SOURCES})
    target_compile_definitions(VoxelEngine PRIVATE BUILD_SHARED)
else()
    add_library(VoxelEngine STATIC ${ENGINE_SOURCES})
endif()

target_include_directories(VoxelEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VoxelEngine PRIVATE spdlog::spdlog)
target_link_libraries(VoxelEngine PRIVATE glfw)
target_link_libraries(VoxelEngine PRIVATE glad::glad)
target_link_libraries(VoxelEngine PRIVATE imgui)
target_link_libraries(VoxelEngine PRIVATE hwy)

if (ENGINE_ENABLE_PROFILING)

    target_link_libraries(VoxelEngine PRIVATE Tracy::TracyClient)

    target_compile_definitions(VoxelEngine PRIVATE
        ENGINE_ENABLE_PROFILING
        TRACY_ENABLE
        TRACY_VK_USE_SYMBOL_TABLE
    )

endif()

# OpenGL system library (REQUIRED)
if(WIN32)
    target_link_libraries(VoxelEngine PRIVATE opengl32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(VoxelEngine PRIVATE GL)
endif()

find_package(Vulkan REQUIRED)
target_link_libraries(VoxelEngine PRIVATE Vulkan::Vulkan)
target_link_libraries(VoxelEngine PRIVATE vma)

if(WIN32)
    target_link_libraries(VoxelEngine PRIVATE d3d12ma)
endif()

target_precompile_headers(VoxelEngine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)
