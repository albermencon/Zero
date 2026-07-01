#include "pch.h"
#ifdef PLATFORM_WINDOWS
#include <Engine/IO/Platform/IOPlatform.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Zero::IO {
    std::expected<FileHandle, std::error_code> PlatformOpenFile(const std::filesystem::path& path, FileAccess access, FileShare share, IOFlags flags) noexcept {
        DWORD dwDesiredAccess = 0;
        DWORD dwShareMode = 0;
        DWORD dwCreationDisposition = 0;
        DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;

        switch (access) {
            case FileAccess::Read:
                dwDesiredAccess = GENERIC_READ;
                dwCreationDisposition = OPEN_EXISTING;
                break;
            case FileAccess::Write:
                dwDesiredAccess = GENERIC_WRITE;
                dwCreationDisposition = CREATE_ALWAYS;
                break;
            case FileAccess::ReadWrite:
                dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
                dwCreationDisposition = OPEN_ALWAYS;
                break;
        }

        switch (share) {
            case FileShare::None: dwShareMode = 0; break;
            case FileShare::Read: dwShareMode = FILE_SHARE_READ; break;
            case FileShare::Write: dwShareMode = FILE_SHARE_WRITE; break;
            case FileShare::All: dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE; break;
        }

        if (HasFlag(flags, IOFlags::Direct)) {
            dwFlagsAndAttributes |= FILE_FLAG_NO_BUFFERING;
        }
        if (HasFlag(flags, IOFlags::WriteThrough)) {
            dwFlagsAndAttributes |= FILE_FLAG_WRITE_THROUGH;
        }
        if (HasFlag(flags, IOFlags::NoCache)) {
            dwFlagsAndAttributes |= FILE_FLAG_SEQUENTIAL_SCAN; 
        }

        HANDLE hFile = CreateFileW(
            path.c_str(),
            dwDesiredAccess,
            dwShareMode,
            nullptr,
            dwCreationDisposition,
            dwFlagsAndAttributes,
            nullptr
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        }

        size_t size = 0;
        LARGE_INTEGER liSize;
        if (GetFileSizeEx(hFile, &liSize)) {
            size = static_cast<size_t>(liSize.QuadPart);
        }

        return FileHandle{ hFile, size };
    }

    void PlatformCloseFile(FileHandle& file) noexcept {
        if (file.handle && file.handle != INVALID_HANDLE_VALUE) {
            CloseHandle(reinterpret_cast<HANDLE>(file.handle));
            file.handle = nullptr;
            file.size = 0;
        }
    }
}
#endif
