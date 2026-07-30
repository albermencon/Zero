#pragma once
#include "IOCommon.h"
#include <Engine/JobSystem/Job.h>
#include <span>

namespace Zero::IO 
{
    struct ScatterRange 
    {
        size_t offset;
        std::span<std::byte> destination;
    };

    struct [[nodiscard]] ReadRequest 
    {
        FileHandle file;
        std::span<std::byte> destination;
        size_t offset = 0;
        const char* debugName = nullptr;
        Job completionJob;
        IOFence fence;
        Priority priority = Priority::Normal;
        AccessPattern access = AccessPattern::Sequential;
        IOFlags flags = IOFlags::None;
    };

    struct [[nodiscard]] WriteRequest 
    {
        FileHandle file;
        std::span<const std::byte> source;
        size_t offset = 0;
        const char* debugName = nullptr;
        Job completionJob;
        IOFence fence;
        Priority priority = Priority::Normal;
        AccessPattern access = AccessPattern::Sequential;
        IOFlags flags = IOFlags::None;
    };

    struct [[nodiscard]] AppendRequest 
    {
        FileHandle file;
        std::span<const std::byte> source;
        const char* debugName = nullptr;
        Job completionJob;
        IOFence fence;
        Priority priority = Priority::Normal;
        IOFlags flags = IOFlags::None;
    };

    struct [[nodiscard]] ReadScatterRequest 
    {
        FileHandle file;
        std::span<const ScatterRange> ranges;
        const char* debugName = nullptr;
        Job completionJob;
        IOFence fence;
        Priority priority = Priority::Normal;
        AccessPattern access = AccessPattern::Sequential;
        IOFlags flags = IOFlags::None;
    };

    struct [[nodiscard]] StreamReadDescriptor 
    {
        FileHandle file;
        std::span<std::byte> destination;
        size_t offset = 0;
        size_t totalSize = 0;
        size_t chunkSize = 1024 * 1024;
        uint32_t slidingWindowSize = 8; // Max concurrent active chunks for this stream
        Priority priority = Priority::Normal;
        IOFlags flags = IOFlags::None;
        Job chunkCompletionJob;         // Receives StreamChunkResult in Job payload
        Job streamCompletionJob;        // Triggered when all chunks finish
        const char* debugName = nullptr;
    };

    struct [[nodiscard]] StreamChunkResult 
    {
        uint32_t chunkIndex{ 0 };
        uint32_t bytesRead{ 0 };
        std::byte* buffer{ nullptr };

        [[nodiscard]] std::span<std::byte> memory() const noexcept
        {
            return { buffer, bytesRead };
        }
    };
    static_assert(sizeof(StreamChunkResult) <= 16, "StreamChunkResult must fit inside 16-byte Job payload");
}
