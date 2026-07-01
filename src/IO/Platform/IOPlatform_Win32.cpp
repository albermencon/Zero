#include "pch.h"
#ifdef PLATFORM_WINDOWS
#include <Engine/IO/Platform/IOPlatform.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Zero::IO 
{
    std::expected<FileHandle, std::error_code> PlatformOpenFile(const std::filesystem::path& path, FileAccess access, FileShare share, IOFlags flags) noexcept 
    {
        DWORD dwDesiredAccess = 0;
        DWORD dwShareMode = 0;
        DWORD dwCreationDisposition = 0;
        DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;

        switch (access) 
        {
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

        switch (share) 
        {
            case FileShare::None: dwShareMode = 0; break;
            case FileShare::Read: dwShareMode = FILE_SHARE_READ; break;
            case FileShare::Write: dwShareMode = FILE_SHARE_WRITE; break;
            case FileShare::All: dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE; break;
        }

        if (HasFlag(flags, IOFlags::Direct)) 
        {
            dwFlagsAndAttributes |= FILE_FLAG_NO_BUFFERING;
        }
        if (HasFlag(flags, IOFlags::WriteThrough)) 
        {
            dwFlagsAndAttributes |= FILE_FLAG_WRITE_THROUGH;
        }
        if (HasFlag(flags, IOFlags::NoCache)) 
        {
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

        if (hFile == INVALID_HANDLE_VALUE) 
        {
            return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        }

        size_t size = 0;
        LARGE_INTEGER liSize;
        if (GetFileSizeEx(hFile, &liSize)) 
        {
            size = static_cast<size_t>(liSize.QuadPart);
        }

        return FileHandle{ hFile, size };
    }

    void PlatformCloseFile(FileHandle& file) noexcept 
    {
        if (file.handle && file.handle != INVALID_HANDLE_VALUE) 
        {
            CloseHandle(reinterpret_cast<HANDLE>(file.handle));
            file.handle = nullptr;
            file.size = 0;
        }
    }

    std::expected<size_t, std::error_code> PlatformRead(FileHandle handle, void* buffer, size_t size, size_t offset) noexcept 
    {
        OVERLAPPED ov = {};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);

        DWORD bytesRead = 0;
        if (ReadFile(reinterpret_cast<HANDLE>(handle.handle), buffer, static_cast<DWORD>(size), &bytesRead, &ov)) 
        {
            return static_cast<size_t>(bytesRead);
        }

        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) 
        {
            if (GetOverlappedResult(reinterpret_cast<HANDLE>(handle.handle), &ov, &bytesRead, TRUE)) 
            {
                return static_cast<size_t>(bytesRead);
            }
            err = GetLastError();
        }
        
        if (err == ERROR_HANDLE_EOF) return 0;
        return std::unexpected(std::error_code(static_cast<int>(err), std::system_category()));
    }

    std::expected<size_t, std::error_code> PlatformWrite(FileHandle handle, const void* buffer, size_t size, size_t offset) noexcept 
    {
        OVERLAPPED ov = {};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);

        DWORD bytesWritten = 0;
        if (WriteFile(reinterpret_cast<HANDLE>(handle.handle), buffer, static_cast<DWORD>(size), &bytesWritten, &ov)) 
        {
            return static_cast<size_t>(bytesWritten);
        }

        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) 
        {
            if (GetOverlappedResult(reinterpret_cast<HANDLE>(handle.handle), &ov, &bytesWritten, TRUE)) 
            {
                return static_cast<size_t>(bytesWritten);
            }
            err = GetLastError();
        }

        return std::unexpected(std::error_code(static_cast<int>(err), std::system_category()));
    }
}
#endif
