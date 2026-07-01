#pragma once
#include <Engine/File/FileSystem.h>
#include <Engine/IO/IOCommon.h>
#include <filesystem>
#include <system_error>
#include <expected>

namespace Zero::IO {
    // Note: To support async I/O efficiently on all platforms, PlatformOpenFile must 
    // open the handle with the necessary flags (e.g., FILE_FLAG_OVERLAPPED on Windows).
    std::expected<FileHandle, std::error_code> PlatformOpenFile(const std::filesystem::path& path, FileAccess access, FileShare share, IOFlags flags = IOFlags::None) noexcept;
    
    void PlatformCloseFile(FileHandle& handle) noexcept;
}
