# Prevent vendor libraries from installing themselves
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_HEADER_ONLY ON CACHE BOOL "" FORCE)

# Add submodules
add_subdirectory(spdlog)
add_subdirectory(glfw)
add_subdirectory(glad)