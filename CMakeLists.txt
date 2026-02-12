cmake_minimum_required(VERSION 3.20)
project(VoxelEngine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED "Build as shared library" ON)

# We set this variable to tell spdlog not to install itself, just build
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog)

file(GLOB_RECURSE ENGINE_SOURCES
    "src/*.cpp"
)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VoxelEngine PRIVATE spdlog::spdlog)

target_precompile_headers(VoxelEngine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)
