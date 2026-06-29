#include "pch.h"
#include <Engine/File/FileSystem.h>
#include <chrono>
#include <shared_mutex>
#include <mutex>
#include <algorithm>
#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#ifndef PLATFORM_WINDOWS
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace Zero 
{
    struct MountPoint
    {
        std::string virtual_path;
        std::filesystem::path physical_path;
    };

    static std::vector<MountPoint> s_MountPoints;
    static std::shared_mutex s_MountMutex;

    void FileSystem::Mount(std::string_view virtual_path, const std::filesystem::path &physical_path)
    {
        std::unique_lock lock(s_MountMutex);
        std::string vpath(virtual_path);
        if (!vpath.empty() && (vpath.back() == '/' || vpath.back() == '\\'))
        {
            vpath.pop_back();
        }

        auto it = std::find_if(s_MountPoints.begin(), s_MountPoints.end(), [&](const MountPoint& mp) {
            return mp.virtual_path == vpath;
        });

        if (it != s_MountPoints.end())
        {
            it->physical_path = physical_path;
        }
        else
        {
            s_MountPoints.push_back({ vpath, physical_path });
            std::sort(s_MountPoints.begin(), s_MountPoints.end(), [](const MountPoint& a, const MountPoint& b) {
                return a.virtual_path.length() > b.virtual_path.length();
            });
        }
    }

    void FileSystem::Unmount(std::string_view virtual_path)
    {
        std::unique_lock lock(s_MountMutex);
        std::string vpath(virtual_path);
        if (!vpath.empty() && (vpath.back() == '/' || vpath.back() == '\\'))
        {
            vpath.pop_back();
        }

        auto it = std::find_if(s_MountPoints.begin(), s_MountPoints.end(), [&](const MountPoint& mp) {
            return mp.virtual_path == vpath;
        });

        if (it != s_MountPoints.end())
        {
            s_MountPoints.erase(it);
        }
    }

    std::filesystem::path FileSystem::Resolve(std::string_view path)
    {
        std::shared_lock lock(s_MountMutex);
        for (const auto& mp : s_MountPoints)
        {
            if (path.starts_with(mp.virtual_path))
            {
                if (path.length() == mp.virtual_path.length())
                {
                    return mp.physical_path;
                }
                
                char next_char = path[mp.virtual_path.length()];
                if (next_char == '/' || next_char == '\\')
                {
                    std::string_view relative_part = path.substr(mp.virtual_path.length() + 1);
                    return mp.physical_path / relative_part;
                }
            }
        }
        return std::filesystem::path(path);
    }

    bool FileSystem::Exists(const std::filesystem::path &path)
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    bool FileSystem::IsDirectory(const std::filesystem::path &path)
    {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    std::chrono::file_time<std::chrono::file_clock::duration> FileSystem::GetLastWriteTime(const std::filesystem::path &path)
    {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            return {};
        }
        return time;
    }

    bool FileSystem::Delete(const std::filesystem::path &path)
    {
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }
    
    static bool IsValidHandle(const FileHandle& file)
    {
        if (!file.handle) return false;
#ifdef PLATFORM_WINDOWS
        return file.handle != INVALID_HANDLE_VALUE;
#else
        return reinterpret_cast<intptr_t>(file.handle) >= 0;
#endif
    }

    std::expected<FileHandle, std::error_code> FileSystem::Open(const std::filesystem::path &path, FileAccess access)
    {
#ifdef PLATFORM_WINDOWS
        DWORD dwDesiredAccess = 0;
        DWORD dwShareMode = FILE_SHARE_READ;
        DWORD dwCreationDisposition = 0;
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

        HANDLE hFile = CreateFileW(
            path.c_str(),
            dwDesiredAccess,
            dwShareMode,
            nullptr,
            dwCreationDisposition,
            FILE_ATTRIBUTE_NORMAL,
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
#else
        int flags = 0;
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        switch (access)
        {
        case FileAccess::Read:
            flags = O_RDONLY;
            break;
        case FileAccess::Write:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case FileAccess::ReadWrite:
            flags = O_RDWR | O_CREAT;
            break;
        }

        int fd = ::open(path.c_str(), flags, mode);
        if (fd < 0)
        {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        size_t size = 0;
        struct stat st;
        if (::fstat(fd, &st) == 0)
        {
            size = static_cast<size_t>(st.st_size);
        }

        return FileHandle{ reinterpret_cast<void*>(static_cast<intptr_t>(fd)), size };
#endif
    }

    void FileSystem::Close(FileHandle &file)
    {
        if (!IsValidHandle(file))
        {
            return;
        }
#ifdef PLATFORM_WINDOWS
        CloseHandle(reinterpret_cast<HANDLE>(file.handle));
#else
        ::close(static_cast<int>(reinterpret_cast<intptr_t>(file.handle)));
#endif
        file.handle = nullptr;
        file.size = 0;
    }

    std::expected<size_t, std::error_code> FileSystem::Read(FileHandle &file, std::span<std::byte> destination, size_t offset)
    {
        if (!IsValidHandle(file))
        {
            return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        }

        if (destination.empty())
        {
            return 0;
        }
#ifdef PLATFORM_WINDOWS
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
        DWORD bytesRead = 0;
        BOOL result = ReadFile(
            reinterpret_cast<HANDLE>(file.handle),
            destination.data(),
            static_cast<DWORD>(destination.size()),
            &bytesRead,
            &overlapped
        );

        if (!result)
        {
            DWORD err = GetLastError();
            if (err != ERROR_HANDLE_EOF)
            {
                return std::unexpected(std::error_code(static_cast<int>(err), std::system_category()));
            }
        }
        return static_cast<size_t>(bytesRead);
#else
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(file.handle));
        ssize_t bytesRead = ::pread(fd, destination.data(), destination.size(), static_cast<off_t>(offset));
        if (bytesRead < 0)
        {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }
        return static_cast<size_t>(bytesRead);
#endif
    }

    std::expected<size_t, std::error_code> FileSystem::Write(FileHandle &file, std::span<const std::byte> source, size_t offset)
    {
        if (!IsValidHandle(file))
        {
            return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        }

        if (source.empty())
        {
            return 0;
        }
#ifdef PLATFORM_WINDOWS
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
        DWORD bytesWritten = 0;
        BOOL result = WriteFile(
            reinterpret_cast<HANDLE>(file.handle),
            source.data(),
            static_cast<DWORD>(source.size()),
            &bytesWritten,
            &overlapped
        );

        if (!result)
        {
            return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        }

        size_t newEnd = offset + bytesWritten;
        if (newEnd > file.size)
        {
            file.size = newEnd;
        }

        return static_cast<size_t>(bytesWritten);
#else
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(file.handle));
        ssize_t bytesWritten = ::pwrite(fd, source.data(), source.size(), static_cast<off_t>(offset));
        if (bytesWritten < 0)
        {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        size_t newEnd = offset + bytesWritten;
        if (newEnd > file.size)
        {
            file.size = newEnd;
        }
        
        return static_cast<size_t>(bytesWritten);
#endif
    }

    void FileSystem::IterateDirectory(const std::filesystem::path &path, DirectoryCallback callback, void *userData)
    {
        if (!callback) return;

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
        {
            callback(entry, userData);
        }
    }

    std::expected<FileBuffer, std::error_code> FileSystem::ReadEntireFile(const std::filesystem::path &path)
    {
        auto file_res = Open(path, FileAccess::Read);
        if (!file_res)
        {
            return std::unexpected(file_res.error());
        }

        FileHandle& file = *file_res;
        FileBuffer buffer;
        buffer.data.resize(file.size);
        
        auto read_res = Read(file, std::span<std::byte>(buffer.data.data(), buffer.data.size()), 0);
        Close(file);
        if (!read_res)
        {
            return std::unexpected(read_res.error());
        }

        if (*read_res < buffer.data.size())
        {
            buffer.data.resize(*read_res);
        }
        return buffer;
    }

    std::expected<void, std::error_code> FileSystem::WriteEntireFile(const std::filesystem::path &path, std::span<const std::byte> buffer)
    {
        auto file_res = Open(path, FileAccess::Write);
        if (!file_res)
        {
            return std::unexpected(file_res.error());
        }

        FileHandle& file = *file_res;
        auto write_res = Write(file, buffer, 0);
        Close(file);

        if (!write_res)
        {
            return std::unexpected(write_res.error());
        }

        if (*write_res != buffer.size())
        {
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        return {};
    }
}
