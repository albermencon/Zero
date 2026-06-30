#pragma once

namespace Zero
{
    class Mutex
    {
    public:
        Mutex();
        ~Mutex();

        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void Lock();
        void Unlock();
        bool TryLock();

        void lock() { Lock(); }
        void unlock() { Unlock(); }
        bool try_lock() { return TryLock(); }

        void* GetNativeHandle() { return m_storage; }

    private:
        alignas(8) char m_storage[64];
    };
}
