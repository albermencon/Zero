#include "pch.h"
#include <Engine/Ref.h>

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

namespace Zero
{
    static void SpinYield()
    {
#if defined(_MSC_VER)
        _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#endif
    }

    void WeakRefBlock::Lock()
    {
        while (lock.test_and_set(std::memory_order_acquire))
        {
            SpinYield();
        }
    }

    void WeakRefBlock::Unlock()
    {
        lock.clear(std::memory_order_release);
    }

    ReferenceCounted* WeakRefBlock::LockAndObtain()
    {
        Lock();
        ReferenceCounted* rawPtr = ptr.load(std::memory_order_relaxed);
        if (rawPtr && rawPtr->TryIncRefCount())
        {
            Unlock();
            return rawPtr;
        }
        Unlock();
        return nullptr;
    }

    void ReferenceCounted::DecRefCount() const
    {
        if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            WeakRefBlock* block = m_weakBlock.load(std::memory_order_acquire);
            if (block)
            {
                block->Lock();

                block->ptr.store(nullptr, std::memory_order_release);
                block->Unlock();

                if (block->weakRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    delete block;
                }
            }
            delete const_cast<ReferenceCounted*>(this);
        }
    }

    WeakRefBlock* ReferenceCounted::GetOrAllocateWeakBlock() const
    {
        WeakRefBlock* block = m_weakBlock.load(std::memory_order_acquire);
        if (!block)
        {
            WeakRefBlock* newBlock = new WeakRefBlock();
            newBlock->ptr.store(const_cast<ReferenceCounted*>(this), std::memory_order_relaxed);
            newBlock->weakRefCount.store(1, std::memory_order_relaxed);

            WeakRefBlock* expected = nullptr;
            if (!m_weakBlock.compare_exchange_strong(expected, newBlock, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                delete newBlock;
                block = expected;
            }
            else
            {
                block = newBlock;
            }
        }
        return block;
    }
}
