#include "pch.h"
#ifdef PLATFORM_MACOS
#include <Engine/IO/Platform/IOPlatform.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace Zero::IO {
    std::expected<FileHandle, std::error_code> PlatformOpenFile(const std::filesystem::path& path, FileAccess access, FileShare share, IOFlags flags) noexcept {
        int openFlags = 0;
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

        switch (access) {
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

        if (HasFlag(flags, IOFlags::WriteThrough)) {
            openFlags |= O_SYNC;
        }

        int fd = ::open(path.c_str(), openFlags, mode);
        if (fd < 0) {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        if (HasFlag(flags, IOFlags::Direct) || HasFlag(flags, IOFlags::NoCache)) {
            // macOS uses fcntl F_NOCACHE instead of O_DIRECT
            fcntl(fd, F_NOCACHE, 1);
        }

        size_t size = 0;
        struct stat st;
        if (::fstat(fd, &st) == 0) {
            size = static_cast<size_t>(st.st_size);
        }

        return FileHandle{ reinterpret_cast<void*>(static_cast<intptr_t>(fd)), size };
    }

    void PlatformCloseFile(FileHandle& file) noexcept {
        if (file.handle && reinterpret_cast<intptr_t>(file.handle) >= 0) {
            ::close(static_cast<int>(reinterpret_cast<intptr_t>(file.handle)));
            file.handle = nullptr;
            file.size = 0;
        }
    }
}
#endif
