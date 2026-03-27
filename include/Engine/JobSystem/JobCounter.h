#pragma once
#include <Engine/Core.h>
#include <atomic>
#include <cstdint>
#include <semaphore>

namespace Zero
{
    struct ENGINE_API JobCounter
    {
        std::atomic<uint32_t> pending{0};
        std::binary_semaphore semaphore{ 0 };

        void Decrement();

        // Block calling thread until pending hits 0
        // Workers notify via a condvar or futex
        void Wait();

        // Return this counter to the pool.
        void Release();

    private:
        // pool linkage
        JobCounter* m_next{nullptr};
        friend class JobCounterPool;
    };
}
