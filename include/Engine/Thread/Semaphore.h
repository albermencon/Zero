#pragma once

namespace Zero
{
    class Semaphore
    {
    public:
        explicit Semaphore(int initialCount = 0);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        void Wait();
        void Signal();

        void* GetNativeHandle() { return m_storage; }

    private:
        alignas(8) char m_storage[32];
    };
}
