#include "pch.h"
#include <Engine/IO/IOScheduler.h>
#include <atomic>
#include <vector>
#include <cassert>

#if defined(_MSC_VER)
    #define ZERO_FORCEINLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
    #define ZERO_FORCEINLINE inline __attribute__((always_inline))
#else
    #define ZERO_FORCEINLINE inline
#endif

namespace Zero::IO {

    namespace Internal {
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

        struct IORequestPayload 
        {
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

        Impl(const SchedulerConfig& cfg) : config(cfg) {}

        void Init() 
        {
            uint32_t reqCount = config.maxConcurrentRequests + 1;
            slots = std::make_unique<Internal::IOSlot[]>(reqCount);
            payloads = std::make_unique<Internal::IORequestPayload[]>(reqCount);
            slotNextFree = std::make_unique<std::atomic<uint32_t>[]>(reqCount);

            for (uint32_t i = 1; i < config.maxConcurrentRequests; ++i) {
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
            streamFreeHead.store((1ULL << 32) | 1, std::memory_order_relaxed);
        }

        uint32_t AllocateSlot() {
            uint64_t headVal = slotFreeHead.load(std::memory_order_acquire);
            while (true) {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                if (headIndex == 0) return 0; // Out of slots

                uint32_t nextIndex = slotNextFree[headIndex].load(std::memory_order_relaxed);
                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | nextIndex;

                if (slotFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_acquire)) {
                    return headIndex;
                }
            }
        }

        void FreeSlot(uint32_t index) {
            uint64_t headVal = slotFreeHead.load(std::memory_order_relaxed);
            while (true) {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                slotNextFree[index].store(headIndex, std::memory_order_relaxed);

                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | index;

                if (slotFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
            }
        }

        uint32_t AllocateStream() {
            uint64_t headVal = streamFreeHead.load(std::memory_order_acquire);
            while (true) {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                if (headIndex == 0) return 0;

                uint32_t nextIndex = streamNextFree[headIndex].load(std::memory_order_relaxed);
                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | nextIndex;

                if (streamFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_acquire)) {
                    return headIndex;
                }
            }
        }

        void FreeStream(uint32_t index) {
            uint64_t headVal = streamFreeHead.load(std::memory_order_relaxed);
            while (true) {
                uint32_t headIndex = static_cast<uint32_t>(headVal & 0xFFFFFFFF);
                streamNextFree[index].store(headIndex, std::memory_order_relaxed);

                uint32_t newGen = static_cast<uint32_t>((headVal >> 32) + 1);
                uint64_t newVal = (static_cast<uint64_t>(newGen) << 32) | index;

                if (streamFreeHead.compare_exchange_weak(headVal, newVal, std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
            }
        }
    };

    Scheduler::Scheduler(const SchedulerConfig& config) noexcept
        : m_impl(new Impl(config)) {
    }

    Scheduler::~Scheduler() noexcept {
        Shutdown();
        delete m_impl;
    }

    void Scheduler::Init() {
        m_impl->Init();
    }

    void Scheduler::Shutdown() {
        // Shutdown logic (Phase 3 workers)
    }

    // STUBS FOR PHASE 2 - Implementation follows in subsequent phases
    IOHandle Scheduler::Submit(const ReadRequest&) noexcept { return IOHandle{}; }
    IOHandle Scheduler::Submit(const WriteRequest&) noexcept { return IOHandle{}; }
    IOHandle Scheduler::Submit(const AppendRequest&) noexcept { return IOHandle{}; }
    IOHandle Scheduler::SubmitScatter(const ReadScatterRequest&) noexcept { return IOHandle{}; }
    StreamHandle Scheduler::SubmitStream(const StreamReadDescriptor&) noexcept { return StreamHandle{}; }

    void Scheduler::SubmitBatch(std::span<const ReadRequest>, std::span<IOHandle>) noexcept {}
    void Scheduler::SubmitBatch(std::span<const WriteRequest>, std::span<IOHandle>) noexcept {}

    CancelResult Scheduler::Cancel(IOHandle) noexcept { return CancelResult::InvalidHandle; }
    CancelResult Scheduler::CancelStream(StreamHandle) noexcept { return CancelResult::InvalidHandle; }

    IOProgress Scheduler::GetProgress(IOHandle) noexcept { return IOProgress{}; }
    IOProgress Scheduler::GetStreamProgress(StreamHandle) noexcept { return IOProgress{}; }
    std::expected<size_t, std::error_code> Scheduler::GetResult(IOHandle) noexcept { return 0; }
    void Scheduler::RegisterCoroutineSuspension(IOHandle, void*) noexcept {}

    uint32_t Scheduler::TestAllocateSlot() noexcept { return m_impl->AllocateSlot(); }
    void Scheduler::TestFreeSlot(uint32_t index) noexcept { m_impl->FreeSlot(index); }
    uint32_t Scheduler::TestAllocateStream() noexcept { return m_impl->AllocateStream(); }
    void Scheduler::TestFreeStream(uint32_t index) noexcept { m_impl->FreeStream(index); }

}
