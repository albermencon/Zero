#pragma once

namespace Zero
{
    class Mutex;

    class ConditionVariable
    {
    public:
        ConditionVariable();
        ~ConditionVariable();

        ConditionVariable(const ConditionVariable&) = delete;
        ConditionVariable& operator=(const ConditionVariable&) = delete;

        void Wait(Mutex& mutex);
        void NotifyOne();
        void NotifyAll();

        void* GetNativeHandle() { return m_storage; }

    private:
        alignas(8) char m_storage[48];
    };
}
