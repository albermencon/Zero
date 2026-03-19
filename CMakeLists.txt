cmake_minimum_required(VERSION 3.20)
project(Zero)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED "Build as shared library" ON)
option(ENGINE_ENABLE_PROFILING "Enable Tracy profiling" On) # profiling with tracy

file(GLOB_RECURSE ENGINE_SOURCES
    "src/*.cpp"
)

# Remove all platform-specific thread impls from the glob result
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Thread/Platform/.*\\.cpp$")

# Add only the correct one back
if(WIN32)
    list(APPEND ENGINE_SOURCES "src/Thread/Platform/Thread_Win32.cpp")
elseif(ANDROID)
    list(APPEND ENGINE_SOURCES "src/Thread/Platform/Thread_Linux.cpp")
elseif(UNIX AND APPLE)
    list(APPEND ENGINE_SOURCES "src/Thread/Platform/Thread_Mac.cpp")
elseif(UNIX)
    list(APPEND ENGINE_SOURCES "src/Thread/Platform/Thread_Linux.cpp")
else()
    list(APPEND ENGINE_SOURCES "src/Thread/Platform/Thread_Fallback.cpp")
endif()

# Add vendor dependencies
add_subdirectory(third_party)

if(BUILD_SHARED)
    add_library(Zero SHARED ${ENGINE_SOURCES})
    target_compile_definitions(Zero PRIVATE BUILD_SHARED)
else()
    add_library(Zero STATIC ${ENGINE_SOURCES})
endif()

target_include_directories(Zero
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Zero PRIVATE spdlog::spdlog)
target_link_libraries(Zero PRIVATE glfw)
target_link_libraries(Zero PRIVATE glad::glad)
target_link_libraries(Zero PRIVATE imgui)
target_link_libraries(Zero PRIVATE hwy)
target_link_libraries(Zero PRIVATE moodycamel)

if (ENGINE_ENABLE_PROFILING)

    target_link_libraries(Zero PRIVATE Tracy::TracyClient)

    target_compile_definitions(Zero PRIVATE
        ENGINE_ENABLE_PROFILING
        TRACY_ENABLE
        TRACY_VK_USE_SYMBOL_TABLE
    )

endif()

# OpenGL system library (REQUIRED)
if(WIN32)
    target_link_libraries(Zero PRIVATE opengl32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(Zero PRIVATE GL)
endif()

find_package(Vulkan REQUIRED)
target_link_libraries(Zero PRIVATE Vulkan::Vulkan)
target_link_libraries(Zero PRIVATE vma)

if(WIN32)
    target_link_libraries(Zero PRIVATE d3d12ma)
endif()

# pthread needed on Linux/macOS for pthread_setname_np, pthread_setaffinity_np
if(UNIX)
    target_link_libraries(Zero PRIVATE pthread)
endif()

target_precompile_headers(Zero
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h
)
