#pragma once

namespace Zero
{
    template <typename MutexType>
    class ScopedLock
    {
    public:
        explicit ScopedLock(MutexType& mutex) : m_mutex(mutex)
        {
            if constexpr (requires(MutexType m) { m.Lock(); })
            {
                m_mutex.Lock();
            }
            else
            {
                m_mutex.lock();
            }
        }

        ~ScopedLock()
        {
            if constexpr (requires(MutexType m) { m.Unlock(); })
            {
                m_mutex.Unlock();
            }
            else
            {
                m_mutex.unlock();
            }
        }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        MutexType& m_mutex;
    };
}
