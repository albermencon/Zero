#include "pch.h"
#include <Engine/IO/IOScheduler.h>
#include <Engine/Core.h>
#include <Engine/Log.h>
#include <thread>
#include <Engine/JobSystem/JobSystem.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/ScopedLock.h>
#include <Engine/Thread/Semaphore.h>
#include <coroutine>

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

        // Bit layout: [state:8][generation:16][reserved:8]
        struct IOSlot 
        {
            static constexpr uint32_t Pack(IORequestState s, uint16_t gen) noexcept 
            {
                return static_cast<uint32_t>(s) | (static_cast<uint32_t>(gen) << 8);
            }

            std::atomic<uint32_t> packed{ Pack(IORequestState::Free, 1) };

            ZERO_FORCEINLINE IORequestState GetState(std::memory_order order = std::memory_order_acquire) const noexcept 
            {
                return static_cast<IORequestState>(packed.load(order) & 0xFF);
            }

            ZERO_FORCEINLINE uint16_t GetGeneration(std::memory_order order = std::memory_order_acquire) const noexcept 
            {
                return static_cast<uint16_t>((packed.load(order) >> 8) & 0xFFFF);
            }

            bool TransitionState(IORequestState expected, IORequestState desired, uint16_t expectedGen) noexcept 
            {
                uint32_t exp = Pack(expected, expectedGen);
                uint32_t des = Pack(desired, expectedGen);
                return packed.compare_exchange_strong(exp, des, std::memory_order_acq_rel, std::memory_order_acquire);
            }

            void SetState(IORequestState s, uint16_t gen, std::memory_order order = std::memory_order_release) noexcept 
            {
                packed.store(Pack(s, gen), order);
            }

            void IncrementGeneration() noexcept 
            {
                uint32_t old = packed.load(std::memory_order_relaxed);
                uint16_t gen = static_cast<uint16_t>((old >> 8) & 0xFFFF);
                packed.store(Pack(IORequestState::Free, static_cast<uint16_t>(gen + 1)), std::memory_order_release);
            }
        };
        static_assert(sizeof(IOSlot) == 4, "IOSlot must be 4 bytes");

        enum class IOOperation : uint8_t 
        {
            Read,
            Write,
            Append,
            ReadScatter,
            StreamChunk
        };

        // Scatter ranges are copied inline to eliminate lifetime hazards.
        static constexpr size_t kMaxScatterRanges = 8;

        struct alignas(64) IORequestPayload 
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
                    ScatterRange ranges[kMaxScatterRanges];
                    uint32_t rangeCount;
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
            std::atomic<void*> suspendedCoroutineAddress{ nullptr }; 
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

        struct RingBuffer 
        {
            std::unique_ptr<uint32_t[]> buffer;
            uint32_t head{ 0 };
            uint32_t tail{ 0 };
            uint32_t capacity{ 0 };

            void Init(uint32_t cap) 
            {
                capacity = cap;
                buffer = std::make_unique<uint32_t[]>(cap);
                head = 0;
                tail = 0;
            }

            bool Push(uint32_t val) noexcept 
            {
                uint32_t next = (tail + 1) % capacity;
                if (next == head) return false;
                buffer[tail] = val;
                tail = next;
                return true;
            }

            bool Pop(uint32_t& val) noexcept 
            {
                if (head == tail) return false;
                val = buffer[head];
                head = (head + 1) % capacity;
                return true;
            }

            bool Empty() const noexcept { return head == tail; }
        };
    }

    struct Scheduler::Impl 
    {
        SchedulerConfig config;

        std::unique_ptr<Internal::IOSlot[]> slots;
        std::unique_ptr<Internal::IORequestPayload[]> payloads;
        
        std::unique_ptr<std::atomic<uint32_t>[]> slotNextFree;
        alignas(64) std::atomic<uint64_t> slotFreeHead{ 0 };

        std::unique_ptr<Internal::IOStreamState[]> streamSlots;
        std::unique_ptr<std::atomic<uint64_t>[]> streamNextFree;
        alignas(64) std::atomic<uint64_t> streamFreeHead{ 0 };

        // Each lane is cache-line aligned to eliminate false sharing
        // between producer (game threads) and consumer (worker threads) on
        // adjacent priority levels.
        struct alignas(64) PriorityLane 
        {
            Zero::Mutex mutex;
            Internal::RingBuffer queue;
        };

        PriorityLane lanes[5]; // Background(0), Low(1), Normal(2), High(3), Critical(4)
        
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
            slotFreeHead.store((1ULL << 32) | 1, std::memory_order_relaxed);

            streamSlots = std::make_unique<Internal::IOStreamState[]>(config.maxConcurrentStreams + 1);
            streamNextFree = std::make_unique<std::atomic<uint64_t>[]>(config.maxConcurrentStreams + 1);
            for (uint32_t i = 1; i < config.maxConcurrentStreams; ++i) 
            {
                streamNextFree[i].store(i + 1, std::memory_order_relaxed);
            }
            streamNextFree[config.maxConcurrentStreams].store(0, std::memory_order_relaxed);
            streamFreeHead.store((uint64_t(1) << 32) | 1, std::memory_order_relaxed);

            uint32_t perLaneCapacity = config.queueCapacity;
            if (perLaneCapacity == 0) perLaneCapacity = 4096;
            for (auto& lane : lanes) 
            {
                lane.queue.Init(perLaneCapacity);
            }

            uint32_t workerCount = config.workerCount;
            if (config.workerMode == WorkerMode::Automatic) 
            {
                workerCount = std::min<uint32_t>(4, std::max<uint32_t>(1, std::thread::hardware_concurrency() / 4));
            }
            if (workerCount == 0) workerCount = 1;

            shuttingDown.store(false, std::memory_order_relaxed);
            for (uint32_t i = 0; i < workerCount; ++i) 
            {
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
                if (headIndex == 0) return 0;

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
            if (laneIndex < 0 || laneIndex > 4) laneIndex = 2;

            {
                Zero::ScopedLock lock(lanes[laneIndex].mutex);
                lanes[laneIndex].queue.Push(slotIndex);
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

            uint16_t gen = slots[slot].GetGeneration(std::memory_order_relaxed);
            slots[slot].SetState(Internal::IORequestState::Queued, gen);

            SubmitToLane(stream.priority, slot);
        }

        // Workers use TryLock to avoid lock convoy across all 5 lanes.
        // Only one mutex is held at a time, and only if it's uncontended.
        void WorkerLoop() 
        {
            while (!shuttingDown.load(std::memory_order_acquire)) 
            {
                uint32_t handleIndex = 0;
                
                for (int i = 4; i >= 0; --i) 
                {
                    if (lanes[i].mutex.TryLock()) 
                    {
                        bool found = lanes[i].queue.Pop(handleIndex);
                        lanes[i].mutex.Unlock();
                        if (found) break;
                        handleIndex = 0;
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

        //   1. Write results into payload
        //   2. Set terminal state (pollers/awaiters see Completed/Failed)
        //   3. Enqueue completion job
        //   4. Resume suspended coroutine
        //   5. Decrement fence counter (LAST — waker sees fully consistent state)
        //   Slot recycling is deferred to explicit Release(handle) by the caller.
        void ExecuteRequest(uint32_t index) 
        {
            auto& slot = slots[index];
            uint16_t gen = slot.GetGeneration(std::memory_order_relaxed);
            slot.SetState(Internal::IORequestState::Executing, gen);

            auto& payload = payloads[index];

            std::expected<size_t, std::error_code> result = std::unexpected(std::error_code());

            if (payload.operation == Internal::IOOperation::Read) 
            {
                result = PlatformRead(payload.file, payload.single.buffer, payload.requestedBytes, payload.single.offset);
            }
            else if (payload.operation == Internal::IOOperation::Write || payload.operation == Internal::IOOperation::Append) 
            {
                result = PlatformWrite(payload.file, payload.single.buffer, payload.requestedBytes, payload.single.offset);
            }
            else if (payload.operation == Internal::IOOperation::ReadScatter)
            {
                size_t totalRead = 0;
                bool hadError = false;
                for (uint32_t i = 0; i < payload.scatter.rangeCount; ++i) 
                {
                    const auto& range = payload.scatter.ranges[i];
                    auto res = PlatformRead(payload.file, range.destination.data(), range.destination.size(), range.offset);
                    if (!res) 
                    {
                        result = std::unexpected(res.error());
                        hadError = true;
                        break;
                    }
                    totalRead += res.value();
                }
                if (!hadError)
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
                HandleStreamChunkCompletion(index, result.has_value());
            }
            else
            {
                // Step 2: Terminal state visible to pollers
                slot.SetState(result ? Internal::IORequestState::Completed : Internal::IORequestState::Failed, gen);

                // Step 3: Enqueue completion job
                if (payload.completionJob.fn != nullptr) 
                {
                    Zero::Enqueue(payload.completionJob);
                }

                // Step 4: Resume coroutine (atomic exchange prevents double-resume
                // if RegisterCoroutineSuspension races with this completion path)
                void* coro = payload.suspendedCoroutineAddress.exchange(nullptr, std::memory_order_acq_rel);
                if (coro) 
                {
                    std::coroutine_handle<>::from_address(coro).resume();
                }

                // Step 5: Decrement fence (LAST observable side-effect)
                if (payload.fenceCounter) 
                {
                    payload.fenceCounter->Decrement();
                }

                // Slot recycling deferred to Release(handle)
            }
        }

        // §Stream chunk completion with stream slot recycling.
        void HandleStreamChunkCompletion(uint32_t slotIndex, bool success)
        {
            auto& payload = payloads[slotIndex];
            uint32_t streamIndex = payload.stream.streamHandle.GetIndex();
            auto& stream = streamSlots[streamIndex];

            bool validStream = (stream.generation == payload.stream.streamHandle.GetGeneration());
            
            if (validStream)
            {
                if (!success) stream.failed.store(true, std::memory_order_release);

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
                    // Stream slot recycling deferred to ReleaseStream(handle)
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

            // Recycle the per-chunk IO slot regardless of stream validity
            slots[slotIndex].IncrementGeneration();
            FreeSlot(slotIndex);
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
            ZERO_CORE_ASSERT((address & 4095) == 0 && (request.offset & 4095) == 0 && (request.destination.size() & 4095) == 0, 
                "Direct I/O requirements not met: address, offset, and size must be aligned to 4096 bytes");
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
        payload.suspendedCoroutineAddress.store(nullptr, std::memory_order_relaxed);
        payload.errorCode = {};
        payload.bytesTransferred = 0;

        // Read generation BEFORE making slot visible to workers
        uint16_t gen = m_impl->slots[slot].GetGeneration(std::memory_order_relaxed);
        m_impl->slots[slot].SetState(Internal::IORequestState::Queued, gen);
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    IOHandle Scheduler::Submit(const WriteRequest& request) noexcept
    {
        if (HasFlag(request.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(request.source.data());
            ZERO_CORE_ASSERT((address & 4095) == 0 && (request.offset & 4095) == 0 && (request.source.size() & 4095) == 0,
                "Direct I/O requirements not met: address, offset, and size must be aligned to 4096 bytes");
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
        payload.suspendedCoroutineAddress.store(nullptr, std::memory_order_relaxed);
        payload.errorCode = {};
        payload.bytesTransferred = 0;

        uint16_t gen = m_impl->slots[slot].GetGeneration(std::memory_order_relaxed);
        m_impl->slots[slot].SetState(Internal::IORequestState::Queued, gen);
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    IOHandle Scheduler::Submit(const AppendRequest& request) noexcept
    {
        if (HasFlag(request.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(request.source.data());
            size_t appendOffset = request.file.size;
            ZERO_CORE_ASSERT((address & 4095) == 0 && (appendOffset & 4095) == 0 && (request.source.size() & 4095) == 0,
                "Direct I/O requirements not met: address, offset, and size must be aligned to 4096 bytes");
        }

        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::Append;
        payload.file = request.file;
        payload.single.buffer = const_cast<void*>(static_cast<const void*>(request.source.data()));
        payload.single.offset = request.file.size;
        payload.requestedBytes = static_cast<uint32_t>(request.source.size());
        payload.completionJob = request.completionJob;
        payload.fenceCounter = request.fence.counter;
        payload.suspendedCoroutineAddress.store(nullptr, std::memory_order_relaxed);
        payload.errorCode = {};
        payload.bytesTransferred = 0;

        uint16_t gen = m_impl->slots[slot].GetGeneration(std::memory_order_relaxed);
        m_impl->slots[slot].SetState(Internal::IORequestState::Queued, gen);
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    IOHandle Scheduler::SubmitScatter(const ReadScatterRequest& request) noexcept 
    {
        ZERO_CORE_ASSERT(request.ranges.size() <= Internal::kMaxScatterRanges,
            "Scatter read exceeds maximum inline range count");

        if (HasFlag(request.flags, IOFlags::Direct))
        {
            for (const auto& range : request.ranges)
            {
                size_t address = reinterpret_cast<size_t>(range.destination.data());
                ZERO_CORE_ASSERT((address & 4095) == 0 && (range.offset & 4095) == 0 && (range.destination.size() & 4095) == 0,
                    "Direct I/O requirements not met: address, offset, and size must be aligned to 4096 bytes");
            }
        }

        uint32_t slot = m_impl->AllocateSlot();
        if (slot == 0) return IOHandle{ 0 };

        auto& payload = m_impl->payloads[slot];
        payload.operation = Internal::IOOperation::ReadScatter;
        payload.file = request.file;

        // Copy scatter ranges inline to decouple from caller lifetime
        payload.scatter.rangeCount = static_cast<uint32_t>(request.ranges.size());
        for (size_t i = 0; i < request.ranges.size(); ++i) 
        {
            payload.scatter.ranges[i] = request.ranges[i];
        }
        
        size_t totalBytes = 0;
        for (const auto& range : request.ranges) 
        {
            totalBytes += range.destination.size();
        }
        payload.requestedBytes = static_cast<uint32_t>(totalBytes);
        
        payload.completionJob = request.completionJob;
        payload.fenceCounter = request.fence.counter;
        payload.suspendedCoroutineAddress.store(nullptr, std::memory_order_relaxed);
        payload.errorCode = {};
        payload.bytesTransferred = 0;

        uint16_t gen = m_impl->slots[slot].GetGeneration(std::memory_order_relaxed);
        m_impl->slots[slot].SetState(Internal::IORequestState::Queued, gen);
        IOHandle handle{ (static_cast<uint32_t>(gen) << 16) | slot };

        m_impl->SubmitToLane(request.priority, slot);
        return handle;
    }

    StreamHandle Scheduler::SubmitStream(const StreamReadDescriptor& desc) noexcept 
    {
        if (HasFlag(desc.flags, IOFlags::Direct))
        {
            size_t address = reinterpret_cast<size_t>(desc.destination.data());
            ZERO_CORE_ASSERT((address & 4095) == 0 && (desc.offset & 4095) == 0 && (desc.chunkSize & 4095) == 0 && (desc.totalSize & 4095) == 0,
                "Direct I/O requirements not met: address, offset, chunkSize, and totalSize must be aligned to 4096 bytes");
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

    // Generation is validated atomically with state transition via
    // TransitionState()
    CancelResult Scheduler::Cancel(IOHandle handle) noexcept 
    {
        if (!handle.IsValid()) return CancelResult::InvalidHandle;

        uint32_t index = handle.GetIndex();
        if (index >= m_impl->config.maxConcurrentRequests) return CancelResult::InvalidHandle;

        if (m_impl->slots[index].TransitionState(
                Internal::IORequestState::Queued, 
                Internal::IORequestState::Cancelled,
                handle.GetGeneration())) 
        {
            return CancelResult::Cancelled;
        }

        auto state = m_impl->slots[index].GetState();
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
        if (slot.GetGeneration() != handle.GetGeneration()) return {};

        Internal::IORequestState state = slot.GetState();
        
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
    std::expected<size_t, std::error_code> Scheduler::GetResult(IOHandle handle) noexcept 
    {
        if (!handle.IsValid())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentRequests)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        auto& slot = m_impl->slots[index];
        if (slot.GetGeneration() != handle.GetGeneration())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        auto state = slot.GetState();
        if (state == Internal::IORequestState::Completed)
            return static_cast<size_t>(m_impl->payloads[index].bytesTransferred);
        if (state == Internal::IORequestState::Failed)
            return std::unexpected(m_impl->payloads[index].errorCode);
        if (state == Internal::IORequestState::Cancelled)
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));

        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    }

    void Scheduler::Release(IOHandle handle) noexcept
    {
        if (!handle.IsValid()) return;
        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentRequests) return;

        auto& slot = m_impl->slots[index];
        if (slot.GetGeneration() != handle.GetGeneration()) return;

        auto state = slot.GetState();
        if (state != Internal::IORequestState::Completed && 
            state != Internal::IORequestState::Failed &&
            state != Internal::IORequestState::Cancelled) return;

        slot.IncrementGeneration();
        m_impl->FreeSlot(index);
    }

    void Scheduler::RegisterCoroutineSuspension(IOHandle handle, void* coroutineAddress) noexcept 
    {
        if (!handle.IsValid() || !coroutineAddress) return;

        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentRequests) return;

        auto& slot = m_impl->slots[index];
        if (slot.GetGeneration() != handle.GetGeneration()) return;

        // Publish the coroutine address (release) so the worker thread sees it
        m_impl->payloads[index].suspendedCoroutineAddress.store(coroutineAddress, std::memory_order_release);

        // Acquire-read the slot state. If the worker already set a terminal state
        // (release on packed), our acquire here synchronizes-with that release,
        // meaning the worker has already passed its coroutine-resume step.
        // We must resume ourselves — atomic exchange prevents double-resume.
        auto state = slot.GetState();
        if (state == Internal::IORequestState::Completed ||
            state == Internal::IORequestState::Failed ||
            state == Internal::IORequestState::Cancelled)
        {
            void* addr = m_impl->payloads[index].suspendedCoroutineAddress.exchange(nullptr, std::memory_order_acq_rel);
            if (addr)
            {
                std::coroutine_handle<>::from_address(addr).resume();
            }
        }
    }

    void Scheduler::ReleaseStream(StreamHandle handle) noexcept
    {
        if (!handle.IsValid()) return;
        uint32_t index = handle.GetIndex();
        if (index == 0 || index > m_impl->config.maxConcurrentStreams) return;

        auto& stream = m_impl->streamSlots[index];
        if (stream.generation != handle.GetGeneration()) return;

        uint32_t completed = stream.completedChunks.load(std::memory_order_acquire);
        uint32_t total = stream.totalChunks.load(std::memory_order_acquire);
        if (completed != total) return;

        m_impl->FreeStreamSlot(index);
    }

    uint32_t Scheduler::TestAllocateSlot() noexcept { return m_impl->AllocateSlot(); }
    void Scheduler::TestFreeSlot(uint32_t index) noexcept { m_impl->FreeSlot(index); }
    uint32_t Scheduler::TestAllocateStream() noexcept { return m_impl->AllocateStreamSlot(); }
    void Scheduler::TestFreeStream(uint32_t index) noexcept { m_impl->FreeStreamSlot(index); }

}
