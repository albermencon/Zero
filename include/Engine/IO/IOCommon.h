#pragma once
#include <Engine/File/FileSystem.h>
#include <Engine/JobSystem/JobCounter.h>
#include <system_error>

namespace Zero::IO 
{
    enum class FileShare : uint8_t 
    {
        None,
        Read,
        Write,
        All
    };

    enum class Priority : uint8_t 
    {
        Background,
        Low,
        Normal,
        High,
        Critical
    };

    enum class Status : uint8_t 
    {
        Pending,
        Executing,
        Completed,
        Failed,
        Cancelled
    };

    enum class CancelResult : uint8_t 
    {
        Cancelled,
        AlreadyCompleted,
        AlreadyCancelled,
        InvalidHandle,
        NotCancelable
    };

    enum class AccessPattern : uint8_t 
    {
        Sequential,
        Random
    };

    enum class IOFlags : uint8_t 
    {
        None = 0,
        Direct = 1 << 0,       // Requests uncached I/O where supported by the platform and filesystem
        WriteThrough = 1 << 1, // Flush directly to disk (synchronous write)
        NoCache = 1 << 2       // Suggest page drop optimization after read
    };

    struct IOHandle 
    {
        uint32_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }
        constexpr uint16_t GetIndex() const noexcept { return static_cast<uint16_t>(value & 0xFFFF); }
        constexpr uint16_t GetGeneration() const noexcept { return static_cast<uint16_t>((value >> 16) & 0xFFFF); }

        constexpr bool operator==(const IOHandle& other) const noexcept = default;
    };

    struct StreamHandle 
    {
        uint32_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }
        constexpr uint16_t GetIndex() const noexcept { return static_cast<uint16_t>(value & 0xFFFF); }
        constexpr uint16_t GetGeneration() const noexcept { return static_cast<uint16_t>((value >> 16) & 0xFFFF); }

        constexpr bool operator==(const StreamHandle& other) const noexcept = default;
    };

    struct IOFence 
    {
        JobCounter* counter = nullptr;
    };

    inline constexpr IOFlags operator|(IOFlags lhs, IOFlags rhs) noexcept 
    {
        return static_cast<IOFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    inline constexpr IOFlags operator&(IOFlags lhs, IOFlags rhs) noexcept 
    {
        return static_cast<IOFlags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    inline constexpr IOFlags& operator|=(IOFlags& lhs, IOFlags rhs) noexcept 
    {
        lhs = lhs | rhs;
        return lhs;
    }

    inline constexpr IOFlags& operator&=(IOFlags& lhs, IOFlags rhs) noexcept 
    {
        lhs = lhs & rhs;
        return lhs;
    }

    inline constexpr IOFlags operator~(IOFlags val) noexcept 
    {
        return static_cast<IOFlags>(~static_cast<uint8_t>(val));
    }

    inline constexpr bool HasFlag(IOFlags value, IOFlags flag) noexcept 
    {
        return (value & flag) != IOFlags::None;
    }

    struct IOProgress 
    {
        size_t bytesTransferred = 0;
        size_t totalBytes = 0;
        Status status = Status::Pending;
    };
}
