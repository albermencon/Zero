#pragma once
#include "IOCommon.h"
#include "IODescriptors.h"
#include <expected>

namespace Zero::IO 
{
    enum class WorkerMode : uint8_t 
    {
        Automatic,
        Manual
    };

    struct SchedulerConfig 
    {
        uint32_t maxConcurrentRequests = 2048;
        uint32_t maxConcurrentStreams = 128;
        uint32_t queueCapacity = 4096;
        WorkerMode workerMode = WorkerMode::Automatic;
        uint32_t workerCount = 2; // Ignored if Automatic mode is selected
        size_t defaultChunkSize = 1024 * 1024;
    };

    class Scheduler 
    {
    public:
        explicit Scheduler(const SchedulerConfig& config) noexcept;
        ~Scheduler() noexcept;

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;

        void Init();
        void Shutdown();

        [[nodiscard]] IOHandle Submit(const ReadRequest& request) noexcept;
        [[nodiscard]] IOHandle Submit(const WriteRequest& request) noexcept;
        [[nodiscard]] IOHandle Submit(const AppendRequest& request) noexcept;
        [[nodiscard]] IOHandle SubmitScatter(const ReadScatterRequest& request) noexcept;
        [[nodiscard]] StreamHandle SubmitStream(const StreamReadDescriptor& desc) noexcept;

        // Vectored batch submission for high-performance device APIs
        void SubmitBatch(std::span<const ReadRequest> reads, std::span<IOHandle> outHandles) noexcept;
        void SubmitBatch(std::span<const WriteRequest> writes, std::span<IOHandle> outHandles) noexcept;

        CancelResult Cancel(IOHandle handle) noexcept;
        CancelResult CancelStream(StreamHandle handle) noexcept;

        IOProgress GetProgress(IOHandle handle) noexcept;
        IOProgress GetStreamProgress(StreamHandle handle) noexcept;
        std::expected<size_t, std::error_code> GetResult(IOHandle handle) noexcept;

        void Release(IOHandle handle) noexcept;
        void ReleaseStream(StreamHandle handle) noexcept;

        // Bind raw coroutine pointer cleanly to the pre-allocated payload map slot
        void RegisterCoroutineSuspension(IOHandle handle, void* coroutineAddress) noexcept;

        // Internal Test Helpers
        uint32_t TestAllocateSlot() noexcept;
        void TestFreeSlot(uint32_t index) noexcept;
        uint32_t TestAllocateStream() noexcept;
        void TestFreeStream(uint32_t index) noexcept;

    private:
        struct Impl;
        Impl* m_impl;
    };
}
