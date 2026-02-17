# Prevent vendor libraries from installing themselves
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "" FORCE)

# Add submodules
add_subdirectory(spdlog)
add_subdirectory(glfw)
add_subdirectory(glad)

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
