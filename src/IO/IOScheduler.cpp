#include "pch.h"
#include <Engine/IO/IOScheduler.h>
#include <cassert>
#include <deque>
#include <thread> // only for std::thread::hardware_concurrency
#include <Engine/JobSystem/JobSystem.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/ScopedLock.h>
#include <Engine/Thread/Semaphore.h>

#if defined(_MSC_VER)
    #define ZERO_FORCEINLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
    #define ZERO_FORCEINLINE inline __attribute__((always_inline))
#else
    #define ZERO_FORCEINLINE inline
#endif

namespace Zero::IO 
{

    namespace Internal 
    {
        enum class IORequestState : uint8_t 
        {
            Free = 0,
            Queued,
            Executing,
            Completed,
            Failed,
            Cancelled
        };

        struct alignas(4) IOSlot 
        {
            IORequestState state{ IORequestState::Free };
            uint16_t generation{ 1 };
        };
        static_assert(sizeof(IOSlot) == 4, "IOSlot must be 4 bytes");
        static_assert(std::is_trivially_copyable_v<IOSlot>, "IOSlot must be trivially copyable");

        enum class IOOperation : uint8_t 
        {
            Read,
            Write,
            Append,
            ReadScatter,
            StreamChunk
        };

        struct IORequestPayload 
        {
            IORequestPayload() noexcept : single{nullptr, 0} {}
            FileHandle file;
            union 
            {
                struct 
                {
                    void* buffer;
                    size_t offset;
                } single;
                struct 
                {
                    const ScatterRange* ranges;
                    size_t rangeCount;
                } scatter;
                struct 
                {
                    StreamHandle streamHandle;
                    size_t chunkIndex;
                    void* buffer;
                    size_t offset;
                } stream;
            };
            IOOperation operation{ IOOperation::Read };
            JobCounter* fenceCounter{ nullptr };

            Job completionJob;
            std::error_code errorCode;
            uint32_t bytesTransferred{ 0 };
            uint32_t requestedBytes{ 0 };
            void* platformContext{ nullptr }; 
            void* suspendedCoroutineAddress{ nullptr }; 
        };

        struct alignas(64) IOStreamState 
        {
            std::atomic<uint32_t> nextChunkToSubmit{ 0 };
            std::atomic<uint32_t> activeChunks{ 0 };
            std::atomic<uint32_t> completedChunks{ 0 };
            std::atomic<uint32_t> totalChunks{ 0 };
            std::atomic<bool> cancelled{ false };
            std::atomic<bool> failed{ false };
            
            uint16_t generation{ 1 };
            
            FileHandle file;
            size_t streamOffset{ 0 };
            size_t streamTotalSize{ 0 };
            size_t chunkSize{ 0 };
            std::byte* destination{ nullptr };
            
            Priority priority;
            Job chunkCompletionJob;
            Job streamCompletionJob;
        };
    }

    struct Scheduler::Impl 
    {
        SchedulerConfig config;

        std::unique_ptr<Internal::IOSlot[]> slots;
        std::unique_ptr<Internal::IORequestPayload[]> payloads;
        
        // Lock-free free list for slots (ABA-free with generation counter in high 32 bits)
        std::unique_ptr<std::atomic<uint32_t>[]> slotNextFree;
        alignas(64) std::atomic<uint64_t> slotFreeHead{ 0 };

        std::unique_ptr<Internal::IOStreamState[]> streamSlots;
        std::unique_ptr<std::atomic<uint64_t>[]> streamNextFree;
        alignas(64) std::atomic<uint64_t> streamFreeHead{ 0 };

        struct PriorityLane 
        {
            Zero::Mutex mutex;
            std::deque<uint32_t> queue;
        };

        PriorityLane lanes[5]; // Critical, High, Normal, Low, Background
        
        std::vector<Zero::Thread> workers;
        Zero::Semaphore wakeupSemaphore;
        std::atomic<bool> shuttingDown{ false };

        Impl(const SchedulerConfig& cfg) : config(cfg) {}

        void Init() 
        {
            uint32_t reqCount = config.maxConcurrentRequests + 1;
            slots = std::make_unique<Internal::IOSlot[]>(reqCount);
            payloads = std::make_unique<Internal::IORequestPayload[]>(reqCount);
            slotNextFree = std::make_unique<std::atomic<uint32_t>[]>(reqCount);

            for (uint32_t i = 1; i < config.maxConcurrentRequests; ++i) 
            {
                slotNextFree[i].store(i + 1, std::memory_order_relaxed);
            }
            slotNextFree[config.maxConcurrentRequests].store(0, std::memory_order_relaxed);
            slotFreeHead.store((1ULL << 32) | 1, std::memory_order_relaxed); // gen 1, index 1

            streamSlots = std::make_unique<Internal::IOStreamState[]>(config.maxConcurrentStreams + 1);
            streamNextFree = std::make_unique<std::atomic<uint64_t>[]>(config.maxConcurrentStreams + 1);
            for (uint32_t i = 1; i < config.maxConcurrentStreams; ++i) 
            {
                streamNextFree[i].store(i + 1, std::memory_order_relaxed);
            }
            streamNextFree[config.maxConcurrentStreams].store(0, std::memory_order_relaxed);
            streamFreeHead.store((uint64_t(1) << 32) | 1, std::memory_order_relaxed);

            uint32_t workerCount = config.workerCount;
            if (config.workerMode == WorkerMode::Automatic) 
            {
                // Do not create as many threads as logical cores. Divide by 4, min 1, max 4.
                workerCount = std::min<uint32_t>(4, std::max<uint32_t>(1, std::thread::hardware_concurrency() / 4));
            }
            if (workerCount == 0) workerCount = 1;

            shuttingDown.store(false, std::memory_order_relaxed);
            for (uint32_t i = 0; i < workerCount; ++i) 
            {
                // 64 KB stack size limit
                workers.emplace_back(64 * 1024, [this]() { this->WorkerLoop(); });
            }
        }

        void Shutdown()
        {
            shuttingDown.store(true, std::memory_order_release);
            for (size_t i = 0; i < workers.size(); ++i) 
            {
                wakeupSemaphore.Signal();
            }

            for (auto& worker : workers) 
            {
                if (worker.Joinable()) worker.Join();
            }
            workers.clear();
        }

        uint32_t AllocateSlot()
        {
            uint64_t headVal = slotFreeHead.load(std::memory_order_acquire);
            while (true) 
            {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                if (headIndex == 0) return 0; // Out of slots

                uint32_t nextIndex = slotNextFree[headIndex].load(std::memory_order_relaxed);
                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | nextIndex;

                if (slotFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_acquire)) 
                {
                    return headIndex;
                }
            }
        }

        void FreeSlot(uint32_t index) 
        {
            uint64_t headVal = slotFreeHead.load(std::memory_order_relaxed);
            while (true) 
            {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                slotNextFree[index].store(headIndex, std::memory_order_relaxed);

                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | index;

                if (slotFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_relaxed)) 
                {
                    break;
                }
            }
        }

        uint32_t AllocateStreamSlot() 
        {
            uint64_t head = streamFreeHead.load(std::memory_order_acquire);
            while (true) 
            {
                uint32_t index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                if (index == 0 || index > config.maxConcurrentStreams) return 0;

                uint64_t next = streamNextFree[index].load(std::memory_order_relaxed);
                uint64_t newHead = ((head >> 32) + 1) << 32 | (next & 0xFFFFFFFF);

                if (streamFreeHead.compare_exchange_weak(head, newHead, std::memory_order_release, std::memory_order_acquire)) 
                {
                    return index;
                }
            }
        }

        void FreeStreamSlot(uint32_t index) 
        {
            streamSlots[index].generation++;
            uint64_t head = streamFreeHead.load(std::memory_order_acquire);
            while (true) 
            {
                streamNextFree[index].store(head & 0xFFFFFFFF, std::memory_order_relaxed);
                uint64_t newHead = ((head >> 32) + 1) << 32 | index;
                if (streamFreeHead.compare_exchange_weak(head, newHead, std::memory_order_release, std::memory_order_acquire)) 
                {
                    break;
                }
            }
        }

        void SubmitToLane(Priority priority, uint32_t slotIndex) 
        {
            int laneIndex = static_cast<int>(priority);
            if (laneIndex < 0 || laneIndex > 4) laneIndex = 2; // Default Normal

            {
                Zero::ScopedLock lock(lanes[laneIndex].mutex);
                lanes[laneIndex].queue.push_back(slotIndex);
            }
            wakeupSemaphore.Signal();
        }

        void SubmitStreamChunk(uint32_t streamIndex, size_t chunkIndex)
        {
            auto& stream = streamSlots[streamIndex];
            if (stream.cancelled.load(std::memory_order_acquire) || stream.failed.load(std::memory_order_acquire)) return;

            uint32_t slot = AllocateSlot();
            if (slot == 0) return;

            size_t offset = stream.streamOffset + (chunkIndex * stream.chunkSize);
            size_t size = stream.chunkSize;
            if (offset + size > stream.streamOffset + stream.streamTotalSize)
            {
                size = (stream.streamOffset + stream.streamTotalSize) - offset;
            }

            auto& payload = payloads[slot];
            payload.operation = Internal::IOOperation::StreamChunk;
            payload.file = stream.file;
            payload.stream.streamHandle = StreamHandle{ (static_cast<uint32_t>(stream.generation) << 16) | streamIndex };
            payload.stream.chunkIndex = chunkIndex;
            payload.stream.buffer = stream.destination + (chunkIndex * stream.chunkSize);
            payload.stream.offset = offset;
            payload.requestedBytes = static_cast<uint32_t>(size);
            payload.fenceCounter = nullptr;
            payload.completionJob.fn = nullptr;

            std::atomic_ref<Internal::IORequestState> stateRef(slots[slot].state);
            stateRef.store(Internal::IORequestState::Queued, std::memory_order_release);

            SubmitToLane(stream.priority, slot);
        }

        void WorkerLoop() 
        {
            while (!shuttingDown.load(std::memory_order_acquire)) 
            {
                uint32_t handleIndex = 0;
                
                for (int i = 4; i >= 0; --i) 
                {
                    Zero::ScopedLock lock(lanes[i].mutex);
                    if (!lanes[i].queue.empty()) 
                    {
                        handleIndex = lanes[i].queue.front();
                        lanes[i].queue.pop_front();
                        break;
                    }
                }

                if (handleIndex != 0) 
                {
                    ExecuteRequest(handleIndex);
                } 
                else 
                {
                    wakeupSemaphore.Wait();
                }
            }
        }

        void ExecuteRequest(uint32_t index) 
        {
            std::atomic_ref<Internal::IORequestState> stateRef(slots[index].state);
            stateRef.store(Internal::IORequestState::Executing, std::memory_order_release);

            auto& payload = payloads[index];

            std::expected<size_t, std::error_code> result = std::unexpected(std::error_code());

            if (payload.operation == Internal::IOOperation::Read) 
            {
                result = PlatformRead(payload.file, payload.single.buffer, payload.requestedBytes, payload.single.offset);
            }
            else if (payload.operation == Internal::IOOperation::Write) 
            {
                result = PlatformWrite(payload.file, payload.single.buffer, payload.requestedBytes, payload.single.offset);
            }
            else if (payload.operation == Internal::IOOperation::ReadScatter)
            {
                size_t totalRead = 0;
                for (size_t i = 0; i < payload.scatter.rangeCount; ++i) 
                {
                    const auto& range = payload.scatter.ranges[i];
                    auto res = PlatformRead(payload.file, range.destination.data(), range.destination.size(), range.offset);
                    if (!res) 
                    {
                        result = std::unexpected(res.error());
                        break;
                    }
                    totalRead += res.value();
                }
                if (result.has_value() || totalRead > 0) 
                {
                    result = totalRead; 
                }
            }
            else if (payload.operation == Internal::IOOperation::StreamChunk)
            {
                result = PlatformRead(payload.file, payload.stream.buffer, payload.requestedBytes, payload.stream.offset);
            }

            payload.bytesTransferred = result ? static_cast<uint32_t>(result.value()) : 0;
            if (!result) payload.errorCode = result.error();

            if (payload.operation == Internal::IOOperation::StreamChunk)
            {
                uint32_t streamIndex = payload.stream.streamHandle.GetIndex();
                auto& stream = streamSlots[streamIndex];
                
                if (stream.generation == payload.stream.streamHandle.GetGeneration())
                {
                    if (!result) stream.failed.store(true, std::memory_order_release);

                    if (!stream.cancelled.load(std::memory_order_acquire))
                    {
                        if (stream.chunkCompletionJob.fn != nullptr)
                        {
                            Job chunkJob = stream.chunkCompletionJob;
                            StreamChunkResult chunkResult;
                            chunkResult.chunkIndex = payload.stream.chunkIndex;
                            chunkResult.fileOffset = payload.stream.offset;
                            chunkResult.bytesRead = payload.bytesTransferred;
                            chunkResult.memory = std::span<std::byte>{ static_cast<std::byte*>(payload.stream.buffer), payload.bytesTransferred };
                            
                            memcpy(chunkJob.payload, &chunkResult, sizeof(StreamChunkResult));
                            Zero::Enqueue(chunkJob);
                        }
                    }

                    uint32_t completed = stream.completedChunks.fetch_add(1, std::memory_order_acq_rel) + 1;
                    stream.activeChunks.fetch_sub(1, std::memory_order_acq_rel);

                    uint32_t total = stream.totalChunks.load(std::memory_order_acquire);
                    if (completed == total)
                    {
                        if (stream.streamCompletionJob.fn != nullptr)
                        {
                            Zero::Enqueue(stream.streamCompletionJob);
                        }
                    }
                    else
                    {
                        uint32_t nextChunk = stream.nextChunkToSubmit.fetch_add(1, std::memory_order_acq_rel);
                        if (nextChunk < total && !stream.cancelled.load(std::memory_order_acquire) && !stream.failed.load(std::memory_order_acquire))
                        {
                            stream.activeChunks.fetch_add(1, std::memory_order_acq_rel);
                            SubmitStreamChunk(streamIndex, nextChunk);
                        }
                    }
                }
            }
            else
            {
                if (payload.completionJob.fn != nullptr) 
                {
                    Zero::Enqueue(payload.completionJob);
                }
            }

            stateRef.store(result ? Internal::IORequestState::Completed : Internal::IORequestState::Failed, std::memory_order_release);

            if (payload.fenceCounter) 
            {
                payload.fenceCounter->Decrement();
            }
        }
    };

    Scheduler::Scheduler(const SchedulerConfig& config) noexcept
        : m_impl(new Impl(config)) {}

    Scheduler::~Scheduler() noexcept 
    {
        Shutdown();
        delete m_impl;
    }

    void Scheduler::Init() 
    {
        m_impl->Init();
    }

    void Scheduler::Shutdown() 
    {
        m_impl->Shutdown();
    }

    IOHandle Scheduler::Submit(const ReadRequest& request) noexcept
    {
        if (HasFlag(request.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(request.destination.data());
            assert((address & 4095) == 0 && (request.offset & 4095) == 0 && (request.destination.size() & 4095) == 0);
        }

        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::Read;
        payload.file = request.file;
        payload.single.buffer = request.destination.data();
        payload.single.offset = request.offset;
        payload.requestedBytes = static_cast<uint32_t>(request.destination.size());
        payload.completionJob = request.completionJob;
        payload.fenceCounter = request.fence.counter;

        std::atomic_ref<Internal::IORequestState> stateRef(m_impl->slots[slot].state);
        stateRef.store(Internal::IORequestState::Queued, std::memory_order_release);

        uint16_t gen = m_impl->slots[slot].generation;
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    IOHandle Scheduler::Submit(const WriteRequest& request) noexcept
    {
        if (HasFlag(request.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(request.source.data());
            assert((address & 4095) == 0 && (request.offset & 4095) == 0 && (request.source.size() & 4095) == 0);
        }

        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::Write;
        payload.file = request.file;
        payload.single.buffer = const_cast<void*>(static_cast<const void*>(request.source.data()));
        payload.single.offset = request.offset;
        payload.requestedBytes = static_cast<uint32_t>(request.source.size());
        payload.completionJob = request.completionJob;
        payload.fenceCounter = request.fence.counter;

        std::atomic_ref<Internal::IORequestState> stateRef(m_impl->slots[slot].state);
        stateRef.store(Internal::IORequestState::Queued, std::memory_order_release);

        uint16_t gen = m_impl->slots[slot].generation;
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    IOHandle Scheduler::Submit(const AppendRequest&) noexcept { return IOHandle{}; }

    IOHandle Scheduler::SubmitScatter(const ReadScatterRequest& request) noexcept 
    {
        if (HasFlag(request.flags, IOFlags::Direct))
        {
            for (const auto& range : request.ranges)
            {
                size_t address = reinterpret_cast<size_t>(range.destination.data());
                assert((address & 4095) == 0 && (range.offset & 4095) == 0 && (range.destination.size() & 4095) == 0);
            }
        }

        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::ReadScatter;
        payload.file = request.file;
        payload.scatter.ranges = request.ranges.data();
        payload.scatter.rangeCount = request.ranges.size();
        
        size_t totalBytes = 0;
        for (const auto& range : request.ranges) {
            totalBytes += range.destination.size();
        }
        payload.requestedBytes = static_cast<uint32_t>(totalBytes);
        
        payload.completionJob = request.completionJob;
        payload.fenceCounter = request.fence.counter;

        std::atomic_ref<Internal::IORequestState> stateRef(m_impl->slots[slot].state);
        stateRef.store(Internal::IORequestState::Queued, std::memory_order_release);

        uint16_t gen = m_impl->slots[slot].generation;
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    StreamHandle Scheduler::SubmitStream(const StreamReadDescriptor& desc) noexcept 
    {
        if (HasFlag(desc.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(desc.destination.data());
            assert((address & 4095) == 0 && (desc.offset & 4095) == 0 && (desc.chunkSize & 4095) == 0 && (desc.totalSize & 4095) == 0);
        }

        uint32_t slot = m_impl->AllocateStreamSlot();
        if (slot == 0) return StreamHandle{ 0 };

        auto& stream = m_impl->streamSlots[slot];
        stream.file = desc.file;
        stream.streamOffset = desc.offset;
        stream.streamTotalSize = desc.totalSize;
        stream.chunkSize = desc.chunkSize > 0 ? desc.chunkSize : m_impl->config.defaultChunkSize;
        stream.destination = desc.destination.data();
        stream.priority = desc.priority;
        stream.chunkCompletionJob = desc.chunkCompletionJob;
        stream.streamCompletionJob = desc.streamCompletionJob;

        uint32_t totalChunks = static_cast<uint32_t>((desc.totalSize + stream.chunkSize - 1) / stream.chunkSize);
        stream.totalChunks.store(totalChunks, std::memory_order_relaxed);
        stream.completedChunks.store(0, std::memory_order_relaxed);
        stream.cancelled.store(false, std::memory_order_relaxed);
        stream.failed.store(false, std::memory_order_relaxed);

        uint32_t initialWindow = std::min(desc.slidingWindowSize, totalChunks);
        if (initialWindow == 0) initialWindow = 1;

        stream.nextChunkToSubmit.store(initialWindow, std::memory_order_release);
        stream.activeChunks.store(initialWindow, std::memory_order_release);

        uint16_t gen = stream.generation;
        StreamHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        for (uint32_t i = 0; i < initialWindow; ++i)
        {
            m_impl->SubmitStreamChunk(slot, i);
        }

        return handle;
    }

    void Scheduler::SubmitBatch(std::span<const ReadRequest> reads, std::span<IOHandle> outHandles) noexcept 
    {
        if (reads.size() > outHandles.size()) return;
        
        for (size_t i = 0; i < reads.size(); ++i) 
        {
            outHandles[i] = Submit(reads[i]);
        }
    }

    void Scheduler::SubmitBatch(std::span<const WriteRequest> writes, std::span<IOHandle> outHandles) noexcept 
    {
        if (writes.size() > outHandles.size()) return;
        
        for (size_t i = 0; i < writes.size(); ++i) 
        {
            outHandles[i] = Submit(writes[i]);
        }
    }

    CancelResult Scheduler::Cancel(IOHandle handle) noexcept 
    {
        if (!handle.IsValid()) return CancelResult::InvalidHandle;

        uint32_t index = handle.GetIndex();
        if (index >= m_impl->config.maxConcurrentRequests) return CancelResult::InvalidHandle;

        std::atomic_ref<Internal::IORequestState> stateRef(m_impl->slots[index].state);

        auto expectedState = Internal::IORequestState::Queued;
        if (stateRef.compare_exchange_strong(expectedState, Internal::IORequestState::Cancelled, std::memory_order_release, std::memory_order_relaxed)) 
        {
            if (m_impl->slots[index].generation != handle.GetGeneration()) return CancelResult::AlreadyCompleted;
            return CancelResult::Cancelled;
        }

        auto state = stateRef.load(std::memory_order_acquire);
        if (state == Internal::IORequestState::Completed) return CancelResult::AlreadyCompleted;
        if (state == Internal::IORequestState::Cancelled) return CancelResult::AlreadyCancelled;
        
        return CancelResult::NotCancelable;
    }

    CancelResult Scheduler::CancelStream(StreamHandle handle) noexcept 
    {
        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentStreams) return CancelResult::InvalidHandle;

        auto& stream = m_impl->streamSlots[index];
        if (stream.generation != handle.GetGeneration()) return CancelResult::AlreadyCompleted;

        bool expected = false;
        if (stream.cancelled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            if (stream.completedChunks.load(std::memory_order_acquire) == stream.totalChunks.load(std::memory_order_acquire))
            {
                return CancelResult::AlreadyCompleted;
            }
            return CancelResult::Cancelled;
        }

        return CancelResult::AlreadyCancelled;
    }

    IOProgress Scheduler::GetProgress(IOHandle handle) noexcept 
    {
        IOProgress progress;
        progress.status = Status::Pending;
        if (!handle.IsValid()) return progress;

        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentRequests) return {};

        auto& slot = m_impl->slots[index];
        if (slot.generation != handle.GetGeneration()) return {};

        std::atomic_ref<Internal::IORequestState> stateRef(slot.state);
        Internal::IORequestState state = stateRef.load(std::memory_order_acquire);
        
        progress.bytesTransferred = m_impl->payloads[index].bytesTransferred;
        progress.totalBytes = m_impl->payloads[index].requestedBytes;

        switch (state) 
        {
            case Internal::IORequestState::Completed: progress.status = Status::Completed; break;
            case Internal::IORequestState::Failed:    progress.status = Status::Failed; break;
            case Internal::IORequestState::Cancelled: progress.status = Status::Cancelled; break;
            default:                                  progress.status = Status::Executing; break;
        }

        return progress;
    }

    IOProgress Scheduler::GetStreamProgress(StreamHandle handle) noexcept 
    {
        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentStreams) return {};

        auto& stream = m_impl->streamSlots[index];
        if (stream.generation != handle.GetGeneration()) return {}; 

        IOProgress progress;
        progress.bytesTransferred = stream.completedChunks.load(std::memory_order_acquire) * stream.chunkSize;
        if (progress.bytesTransferred > stream.streamTotalSize) progress.bytesTransferred = stream.streamTotalSize;
        progress.totalBytes = stream.streamTotalSize;

        if (stream.failed.load(std::memory_order_acquire))
            progress.status = Status::Failed;
        else if (stream.cancelled.load(std::memory_order_acquire))
            progress.status = Status::Cancelled;
        else if (stream.completedChunks.load(std::memory_order_acquire) == stream.totalChunks.load(std::memory_order_acquire))
            progress.status = Status::Completed;
        else
            progress.status = Status::Executing;

        return progress;
    }
    std::expected<size_t, std::error_code> Scheduler::GetResult(IOHandle) noexcept { return 0; }
    void Scheduler::RegisterCoroutineSuspension(IOHandle, void*) noexcept {}

    uint32_t Scheduler::TestAllocateSlot() noexcept { return m_impl->AllocateSlot(); }
    void Scheduler::TestFreeSlot(uint32_t index) noexcept { m_impl->FreeSlot(index); }
    uint32_t Scheduler::TestAllocateStream() noexcept { return m_impl->AllocateStreamSlot(); }
    void Scheduler::TestFreeStream(uint32_t index) noexcept { m_impl->FreeStreamSlot(index); }

}
