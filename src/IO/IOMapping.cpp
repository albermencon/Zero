#include "pch.h"
#include <Engine/IO/IOMapping.h>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace Zero::IO 
{

    MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
        : m_mappedAddress(other.m_mappedAddress),
          m_mappedSize(other.m_mappedSize),
          m_osHandle(other.m_osHandle),
          m_allocationOffset(other.m_allocationOffset)
    {
        other.m_mappedAddress = nullptr;
        other.m_mappedSize = 0;
        other.m_osHandle = nullptr;
        other.m_allocationOffset = 0;
    }

    MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept 
    {
        if (this != &other) 
        {
            Unmap();
            m_mappedAddress = other.m_mappedAddress;
            m_mappedSize = other.m_mappedSize;
            m_osHandle = other.m_osHandle;
            m_allocationOffset = other.m_allocationOffset;

            other.m_mappedAddress = nullptr;
            other.m_mappedSize = 0;
            other.m_osHandle = nullptr;
            other.m_allocationOffset = 0;
        }
        return *this;
    }

    void MemoryMappedFile::Unmap() noexcept 
    {
        if (m_mappedAddress) 
        {
#if defined(_WIN32)
            void* baseAddress = static_cast<uint8_t*>(m_mappedAddress) - m_allocationOffset;
            ::UnmapViewOfFile(baseAddress);
            if (m_osHandle) 
            {
                ::CloseHandle(static_cast<HANDLE>(m_osHandle));
                m_osHandle = nullptr;
            }
#else
            void* baseAddress = static_cast<uint8_t*>(m_mappedAddress) - m_allocationOffset;
            ::munmap(baseAddress, m_mappedSize + m_allocationOffset);
#endif
            m_mappedAddress = nullptr;
            m_mappedSize = 0;
            m_allocationOffset = 0;
        }
    }

    std::expected<MemoryMappedFile, std::error_code> MemoryMappedFile::Map(FileHandle file, size_t offset, size_t size, bool writeAccess) noexcept 
    {
        MemoryMappedFile mappedFile;
        if (!file.handle) 
        {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

#if defined(_WIN32)
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        size_t granularity = sysInfo.dwAllocationGranularity;

        size_t alignedOffset = (offset / granularity) * granularity;
        size_t diff = offset - alignedOffset;
        size_t mapSize = size + diff;

        DWORD protect = writeAccess ? PAGE_READWRITE : PAGE_READONLY;
        DWORD mapAccess = writeAccess ? FILE_MAP_WRITE : FILE_MAP_READ;

        HANDLE hMap = CreateFileMappingW(reinterpret_cast<HANDLE>(file.handle), nullptr, protect, 0, 0, nullptr);
        if (!hMap) 
        {
            return std::unexpected(std::error_code(GetLastError(), std::system_category()));
        }

        DWORD offsetHigh = static_cast<DWORD>(alignedOffset >> 32);
        DWORD offsetLow = static_cast<DWORD>(alignedOffset & 0xFFFFFFFF);

        void* mappedPtr = MapViewOfFile(hMap, mapAccess, offsetHigh, offsetLow, mapSize);
        if (!mappedPtr) 
        {
            DWORD err = GetLastError();
            CloseHandle(hMap);
            return std::unexpected(std::error_code(err, std::system_category()));
        }

        mappedFile.m_osHandle = hMap;
        mappedFile.m_mappedAddress = static_cast<uint8_t*>(mappedPtr) + diff;
        mappedFile.m_mappedSize = size;
        mappedFile.m_allocationOffset = diff;

#else
        long pageSize = sysconf(_SC_PAGE_SIZE);
        size_t alignedOffset = (offset / pageSize) * pageSize;
        size_t diff = offset - alignedOffset;
        size_t mapSize = size + diff;

        int prot = PROT_READ;
        if (writeAccess) prot |= PROT_WRITE;

        int fd = static_cast<int>(reinterpret_cast<intptr_t>(file.handle) - 1);
        void* mappedPtr = ::mmap(nullptr, mapSize, prot, MAP_SHARED, fd, static_cast<off_t>(alignedOffset));
        
        if (mappedPtr == MAP_FAILED) 
        {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        mappedFile.m_mappedAddress = static_cast<uint8_t*>(mappedPtr) + diff;
        mappedFile.m_mappedSize = size;
        mappedFile.m_allocationOffset = diff;
#endif

        return mappedFile;
    }
}
