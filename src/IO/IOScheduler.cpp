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
            IORequestState state;
            uint16_t generation;
        };
        static_assert(sizeof(IOSlot) == 4, "IOSlot must be 4 bytes");
        static_assert(std::is_trivially_copyable_v<IOSlot>, "IOSlot must be trivially copyable");

        enum class IOOperation : uint8_t 
        {
            Read,
            Write,
            Append,
            ReadScatter
        };

        struct IORequestPayload 
        {
            FileHandle file;
            void* buffer{ nullptr };
            size_t offset{ 0 };
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
            std::atomic<uint32_t> activeChunks{ 0 };
            std::atomic<uint32_t> completedChunks{ 0 };
            std::atomic<uint32_t> totalChunks{ 0 };
            std::atomic<uint32_t> bytesRead{ 0 };
            std::atomic<bool> failed{ false };
            uint16_t generation{ 1 };
            Job streamFinishedJob;
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

        std::unique_ptr<Internal::IOStreamState[]> streams;
        std::unique_ptr<std::atomic<uint32_t>[]> streamNextFree;
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
                slots[i].state = Internal::IORequestState::Free;
                slots[i].generation = 1;
                slotNextFree[i].store(i + 1, std::memory_order_relaxed);
            }
            slotNextFree[config.maxConcurrentRequests].store(0, std::memory_order_relaxed);
            slotFreeHead.store((1ULL << 32) | 1, std::memory_order_relaxed); // gen 1, index 1

            uint32_t streamCount = config.maxConcurrentStreams + 1;
            streams = std::make_unique<Internal::IOStreamState[]>(streamCount);
            streamNextFree = std::make_unique<std::atomic<uint32_t>[]>(streamCount);
            
            for (uint32_t i = 1; i < config.maxConcurrentStreams; ++i) 
            {
                streams[i].generation = 1;
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

        uint32_t AllocateStream() 
        {
            uint64_t headVal = streamFreeHead.load(std::memory_order_acquire);
            while (true) 
            {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                if (headIndex == 0) return 0;

                uint32_t nextIndex = streamNextFree[headIndex].load(std::memory_order_relaxed);
                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | nextIndex;

                if (streamFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_acquire))
                {
                    return headIndex;
                }
            }
        }

        void FreeStream(uint32_t index) 
        {
            uint64_t headVal = streamFreeHead.load(std::memory_order_relaxed);
            while (true) 
            {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                streamNextFree[index].store(headIndex, std::memory_order_relaxed);

                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | index;

                if (streamFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_relaxed)) 
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

        void WorkerLoop() 
        {
            while (!shuttingDown.load(std::memory_order_acquire)) 
            {
                uint32_t handleIndex = 0;
                
                // Pop highest priority (4 = Critical, 0 = Background)
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
                result = PlatformRead(payload.file, payload.buffer, payload.requestedBytes, payload.offset);
            }
            else if (payload.operation == Internal::IOOperation::Write) 
            {
                result = PlatformWrite(payload.file, payload.buffer, payload.requestedBytes, payload.offset);
            }

            if (result) 
            {
                payload.bytesTransferred = static_cast<uint32_t>(result.value());
                stateRef.store(Internal::IORequestState::Completed, std::memory_order_release);
            } 
            else 
            {
                payload.errorCode = result.error();
                stateRef.store(Internal::IORequestState::Failed, std::memory_order_release);
            }

            if (payload.fenceCounter) 
            {
                payload.fenceCounter->Decrement();
            }

            if (payload.completionJob.fn) 
            {
                Zero::Enqueue(payload.completionJob);
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

    // STUBS FOR PHASE 2 - Implementation follows in subsequent phases
    IOHandle Scheduler::Submit(const ReadRequest& request) noexcept
    {
        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::Read;
        payload.file = request.file;
        payload.buffer = request.destination.data();
        payload.requestedBytes = static_cast<uint32_t>(request.destination.size());
        payload.offset = request.offset;
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
        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::Write;
        payload.file = request.file;
        payload.buffer = const_cast<void*>(static_cast<const void*>(request.source.data()));
        payload.requestedBytes = static_cast<uint32_t>(request.source.size());
        payload.offset = request.offset;
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
    IOHandle Scheduler::SubmitScatter(const ReadScatterRequest&) noexcept { return IOHandle{}; }
    StreamHandle Scheduler::SubmitStream(const StreamReadDescriptor&) noexcept { return StreamHandle{}; }

    void Scheduler::SubmitBatch(std::span<const ReadRequest>, std::span<IOHandle>) noexcept {}
    void Scheduler::SubmitBatch(std::span<const WriteRequest>, std::span<IOHandle>) noexcept {}

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

    CancelResult Scheduler::CancelStream(StreamHandle) noexcept { return CancelResult::InvalidHandle; }

    IOProgress Scheduler::GetProgress(IOHandle handle) noexcept 
    {
        IOProgress progress;
        if (!handle.IsValid()) return progress;

        uint32_t index = handle.GetIndex();
        if (index >= m_impl->config.maxConcurrentRequests) return progress;

        std::atomic_ref<Internal::IORequestState> stateRef(m_impl->slots[index].state);
        auto state = stateRef.load(std::memory_order_acquire);
        auto gen = m_impl->slots[index].generation;

        if (gen != handle.GetGeneration()) 
        {
            progress.status = Status::Completed; 
            return progress;
        }

        switch (state) 
        {
            case Internal::IORequestState::Free: progress.status = Status::Completed; break;
            case Internal::IORequestState::Queued: progress.status = Status::Pending; break;
            case Internal::IORequestState::Executing: progress.status = Status::Executing; break;
            case Internal::IORequestState::Completed: progress.status = Status::Completed; break;
            case Internal::IORequestState::Failed: progress.status = Status::Failed; break;
            case Internal::IORequestState::Cancelled: progress.status = Status::Cancelled; break;
        }

        progress.bytesTransferred = m_impl->payloads[index].bytesTransferred;
        progress.totalBytes = m_impl->payloads[index].requestedBytes;

        return progress;
    }
    IOProgress Scheduler::GetStreamProgress(StreamHandle) noexcept { return IOProgress{}; }
    std::expected<size_t, std::error_code> Scheduler::GetResult(IOHandle) noexcept { return 0; }
    void Scheduler::RegisterCoroutineSuspension(IOHandle, void*) noexcept {}

    uint32_t Scheduler::TestAllocateSlot() noexcept { return m_impl->AllocateSlot(); }
    void Scheduler::TestFreeSlot(uint32_t index) noexcept { m_impl->FreeSlot(index); }
    uint32_t Scheduler::TestAllocateStream() noexcept { return m_impl->AllocateStream(); }
    void Scheduler::TestFreeStream(uint32_t index) noexcept { m_impl->FreeStream(index); }

}
