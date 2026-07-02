#include "pch.h"
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
#include <Engine/IO/Platform/IOPlatform.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace Zero::IO 
{
    std::expected<FileHandle, std::error_code> PlatformOpenFile(const std::filesystem::path& path, FileAccess access, FileShare share, IOFlags flags) noexcept 
    {
        int openFlags = 0;
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

        switch (access) 
        {
            case FileAccess::Read:
                openFlags = O_RDONLY;
                break;
            case FileAccess::Write:
                openFlags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case FileAccess::ReadWrite:
                openFlags = O_RDWR | O_CREAT;
                break;
        }

        if (HasFlag(flags, IOFlags::Direct)) 
        {
#if defined(O_DIRECT)
            openFlags |= O_DIRECT;
#endif
        }
        if (HasFlag(flags, IOFlags::WriteThrough)) 
        {
            openFlags |= O_SYNC;
        }

        int fd = ::open(path.c_str(), openFlags, mode);
        if (fd < 0) 
        {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        if (HasFlag(flags, IOFlags::NoCache)) 
        {
#if defined(POSIX_FADV_DONTNEED)
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
        }

        size_t size = 0;
        struct stat st;
        if (::fstat(fd, &st) == 0) 
        {
            size = static_cast<size_t>(st.st_size);
        }

        // note: fd 0 is valid on POSIX but maps to nullptr via void*.
        // Store fd+1 to reserve nullptr as the invalid sentinel.
        return FileHandle{ reinterpret_cast<void*>(static_cast<intptr_t>(fd + 1)), size };
    }

    void PlatformCloseFile(FileHandle& file) noexcept 
    {
        intptr_t stored = reinterpret_cast<intptr_t>(file.handle);
        if (stored > 0) 
        {
            ::close(static_cast<int>(stored - 1));
            file.handle = nullptr;
            file.size = 0;
        }
    }

    // Retry loop handles short reads and EINTR transparently.
    std::expected<size_t, std::error_code> PlatformRead(FileHandle handle, void* buffer, size_t size, size_t offset) noexcept 
    {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle.handle) - 1);
        auto* p = static_cast<uint8_t*>(buffer);
        size_t remaining = size;
        
        while (remaining > 0) 
        {
            ssize_t n = ::pread(fd, p, remaining, static_cast<off_t>(offset));
            if (n < 0) 
            {
                if (errno == EINTR) continue;
                return std::unexpected(std::error_code(errno, std::generic_category()));
            }
            if (n == 0) break;
            p += n;
            offset += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return size - remaining;
    }

    std::expected<size_t, std::error_code> PlatformWrite(FileHandle handle, const void* buffer, size_t size, size_t offset) noexcept 
    {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle.handle) - 1);
        auto* p = static_cast<const uint8_t*>(buffer);
        size_t remaining = size;
        
        while (remaining > 0) 
        {
            ssize_t n = ::pwrite(fd, p, remaining, static_cast<off_t>(offset));
            if (n < 0) 
            {
                if (errno == EINTR) continue;
                return std::unexpected(std::error_code(errno, std::generic_category()));
            }
            if (n == 0) break;
            p += n;
            offset += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return size - remaining;
    }
}
#endif
