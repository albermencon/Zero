#include <doctest.h>
#include <Engine/IO/IOScheduler.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace Zero;
using namespace Zero::IO;

TEST_CASE("IOScheduler: Lock-Free Allocator Sequential") {
    SchedulerConfig config;
    config.maxConcurrentRequests = 10;
    config.maxConcurrentStreams = 5;
    
    Scheduler scheduler(config);
    scheduler.Init();

    std::vector<uint32_t> allocated;
    for (int i = 0; i < 10; ++i) { // maxConcurrentRequests is 10, meaning exactly 10 available slots
        uint32_t slot = scheduler.TestAllocateSlot();
        CHECK(slot != 0);
        allocated.push_back(slot);
    }

    uint32_t exhausted = scheduler.TestAllocateSlot();
    CHECK(exhausted == 0); // Out of slots

    for (uint32_t slot : allocated) {
        scheduler.TestFreeSlot(slot);
    }

    uint32_t slotAgain = scheduler.TestAllocateSlot();
    CHECK(slotAgain != 0);
    scheduler.TestFreeSlot(slotAgain);
}

TEST_CASE("IOScheduler: Lock-Free Allocator Concurrent") {
    SchedulerConfig config;
    config.maxConcurrentRequests = 2000;
    
    Scheduler scheduler(config);
    scheduler.Init();

    const int numThreads = 8;
    const int allocsPerThread = 200; // total 1600 allocations

    std::atomic<int> readyCount = 0;
    std::atomic<bool> startFlag = false;

    auto threadFunc = [&]() {
        readyCount++;
        while (!startFlag) {
            std::this_thread::yield();
        }

        std::vector<uint32_t> localAllocated;
        localAllocated.reserve(allocsPerThread);

        for (int i = 0; i < allocsPerThread; ++i) {
            uint32_t slot = scheduler.TestAllocateSlot();
            if (slot != 0) {
                localAllocated.push_back(slot);
            }
        }

        // Return them
        for (uint32_t slot : localAllocated) {
            scheduler.TestFreeSlot(slot);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(threadFunc);
    }

    while (readyCount < numThreads) {
        std::this_thread::yield();
    }
    
    startFlag = true;

    for (auto& t : threads) {
        t.join();
    }

    // Finally, ensure we can allocate exactly config.maxConcurrentRequests slots
    uint32_t count = 0;
    while (scheduler.TestAllocateSlot() != 0) {
        count++;
    }
    CHECK(count == config.maxConcurrentRequests);
}
