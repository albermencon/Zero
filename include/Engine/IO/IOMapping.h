#pragma once
#include <Engine/File/FileSystem.h>
#include <system_error>
#include <expected>

namespace Zero::IO {
    class MemoryMappedFile 
    {
    public:
        MemoryMappedFile() noexcept = default;
        ~MemoryMappedFile() noexcept { Unmap(); }

        MemoryMappedFile(const MemoryMappedFile&) = delete;
        MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

        MemoryMappedFile(MemoryMappedFile&& other) noexcept;
        MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

        static std::expected<MemoryMappedFile, std::error_code> Map(FileHandle file, size_t offset, size_t size, bool writeAccess = false) noexcept;
        void Unmap() noexcept;

        void* GetAddress() const noexcept { return m_mappedAddress; }
        size_t GetSize() const noexcept { return m_mappedSize; }
        bool IsMapped() const noexcept { return m_mappedAddress != nullptr; }

    private:
        void* m_mappedAddress = nullptr;
        size_t m_mappedSize = 0;
        void* m_osHandle = nullptr;     // Used for Windows Mapping Handle
        size_t m_allocationOffset = 0;  // Offset required to align to OS granularity
    };
}
